// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_SUPPORT_REQSTORE_H_
#define GYRO_SUPPORT_REQSTORE_H_

#include <cstring>
#include <new>
#include <type_traits>

#include <gyro/allocator.h>

#include "request_internal.h"

namespace gyro::support {
    // Acquire() wipes a slot with memset rather than field by field, so that a
    // field added to GyroRequest is covered without anyone having to remember
    // it. That trade holds only while the type stays trivial.
    static_assert(std::is_trivially_copyable_v<GyroRequest>,
                  "RequestStore clears slots with memset; GyroRequest must stay trivially copyable");

    /**
     * @brief Pool of requests, addressed by a token that never dangles.
     *
     * Requests live in pages that are never moved, because their addresses are
     * held by the handle queues, by the timer heap and by the kernel itself.
     * Only the page directory grows, and it holds nothing but pointers.
     *
     * The free list and the token table are the same structure: a token is the
     * position of a request in the pool, paired with the generation of that
     * slot. Releasing a request bumps its generation, so every token naming an
     * earlier occupant is recognisably stale rather than naming whoever took
     * its place.
     *
     * Growth is by whole pages, on demand: an idle store allocates nothing.
     * The store never shrinks.
     */
    class RequestStore {
        static constexpr uint32_t kPageShift = 8;
        static constexpr uint32_t kPageSize = 1u << kPageShift;
        static constexpr uint32_t kPageMask = kPageSize - 1;
        static constexpr uint32_t kIndexBits = 24;
        static constexpr uint32_t kNoSlot = 0xFFFFFFFF;

        const gyro_allocator_t *allocator_ = nullptr;

        /// Page directory. It moves when it grows; the pages it points to do not.
        GyroRequest **pages_ = nullptr;

        uint32_t ndir_ = 0; ///< Entries the directory can hold.
        uint32_t npages_ = 0; ///< Entries actually in use.
        uint32_t nslots_ = 0; ///< npages_ * kPageSize, kept to spare a shift on lookup.
        uint32_t next_free_ = kNoSlot; ///< Head of the free list, threaded through the slots.
        uint32_t max_slots_ = 1 << kIndexBits; ///< Ceiling: an index has to fit kIndexBits.

        /**
         * @brief Appends one page of requests, growing the directory if needed.
         *
         * The directory is enlarged before the page is allocated, so a failure
         * of either leaves the store coherent: at worst the directory is larger
         * than it needs to be. The page is published before its slots are
         * threaded into the free list, so the free list never points into a
         * page the directory does not know about.
         *
         * @return False if the ceiling has been reached or an allocation failed.
         */
        bool Grow() {
            // Room for a whole page, not just for one slot. The subtraction is
            // deliberate: the equivalent addition could overflow.
            if (this->nslots_ > this->max_slots_ - kPageSize)
                return false;

            if (this->npages_ == this->ndir_) {
                const auto wanted = this->ndir_ == 0 ? 8 : this->ndir_ * 2;

                auto **tmp = (GyroRequest **) this->allocator_->alloc(wanted * sizeof(GyroRequest *),
                                                                      this->allocator_->ctx);
                if (tmp == nullptr)
                    return false;

                if (this->pages_ != nullptr) {
                    memcpy(tmp, this->pages_, this->ndir_ * sizeof(GyroRequest *));

                    this->allocator_->free(this->pages_, this->allocator_->ctx);
                }

                this->pages_ = tmp;
                this->ndir_ = wanted;
            }

            // Alloc page
            auto *page = (GyroRequest *) this->allocator_->
                    alloc(sizeof(GyroRequest) * kPageSize, this->allocator_->ctx);
            if (page == nullptr)
                return false;

            this->pages_[this->npages_] = page;
            this->npages_++;
            this->nslots_ = this->npages_ << kPageShift;

            // Backwards, so the lowest indices are handed out first.
            for (auto i = kPageSize; i-- > 0;) {
                auto *req = new(page + i)GyroRequest();

                req->generation = 1;
                req->index = ((this->npages_ - 1) << kPageShift) | i;
                req->next_free = this->next_free_;

                this->next_free_ = req->index;
            }

            return true;
        }

        /**
         * @brief Returns the request at an index, which must be in range.
         */
        [[nodiscard]] GyroRequest *At(const uint32_t token) const noexcept {
            return this->pages_[token >> kPageShift] + (token & kPageMask);
        }

    public:
        /**
         * @brief Builds an empty store.
         *
         * @param allocator Must outlive the store, and is used for every page.
         */
        explicit RequestStore(const gyro_allocator_t *allocator) : allocator_(allocator) {
        }

        ~RequestStore() {
            for (auto i = 0u; i < this->npages_; i++) {
                for (auto j = 0u; j < kPageSize; j++)
                    this->pages_[i][j].~GyroRequest();

                this->allocator_->free(this->pages_[i], this->allocator_->ctx);
            }

            this->allocator_->free(this->pages_, this->allocator_->ctx);
        }

        /**
         * @brief Takes a request out of the store.
         *
         * The request comes back blank, whatever its previous occupant left in
         * it, so callers only have to set the fields their operation uses.
         *
         * @param out_token Receives the token naming the returned request. Left
         *                  untouched when the store cannot satisfy the request.
         * @return The request, or nullptr if the ceiling was reached or an
         *         allocation failed.
         */
        GyroRequest *Acquire(RequestIndex &out_token) noexcept {
            if (this->next_free_ == kNoSlot && !this->Grow())
                return nullptr;

            auto *req = this->At(this->next_free_);

            this->next_free_ = req->next_free;

            out_token.fields.generation = req->generation;
            out_token.fields.index = req->index;

            // A released slot keeps whatever its last occupant left in it, and a
            // stale 'cancelled' or 'cancel_on_timeout' would quietly ruin the
            // next operation to land here. Hand out a blank request instead of
            // trusting every caller to overwrite every field.
            memset(req, 0, sizeof(GyroRequest));

            // Identity belongs to the slot, not to the operation: restoring it
            // is what keeps the token just handed out meaningful.
            req->generation = out_token.fields.generation;
            req->index = out_token.fields.index;

            return req;
        }

        /**
         * @brief Looks up the request a token names.
         *
         * @return The request, or nullptr if the token is stale, was never
         *         valid, or names a slot this store does not have.
         */
        [[nodiscard]] GyroRequest *Resolve(const RequestIndex index) const noexcept {
            if (index.fields.index >= this->nslots_)
                return nullptr;

            auto *req = this->At(index.fields.index);

            if (req->generation != index.fields.generation)
                return nullptr;

            return req;
        }

        /**
         * @brief Returns a request to the store, invalidating its token.
         *
         * @warning Must be called once per Acquire: releasing twice threads the slot
         * into the free list twice, and the store then hands it to two callers.
         */
        void Release(GyroRequest *request) noexcept {
            request->generation += 1;
            request->next_free = this->next_free_;

            this->next_free_ = request->index;
        }
    };
} // namespace gyro::support

#endif // !GYRO_SUPPORT_REQSTORE_H_
