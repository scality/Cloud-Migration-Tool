// Copyright (c) 2011, David Pineau
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER AND CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef __CLOUDMIG_SYNCED_DIR_H__
#define __CLOUDMIG_SYNCED_DIR_H__

struct synceddir
{
    struct synceddir_ctx    *ctx;
    char                    *path;
    int                     pathlen;

    int                     refcount;
    bool                    exists;
    bool                    done;
    pthread_cond_t          notify_cond;

    struct synceddir        *next;
    struct synceddir        *prev;
};

struct synceddir_ctx
{
    pthread_mutex_t         lock;

    struct {
        struct synceddir        *first;
        struct synceddir        *last;
    }                       list;
};

/*
 * @brief Create a synchronized directory creator context
 *
 * @return The context  The context is properly set up
 *         NULL         The context could not be allocated
 */
struct synceddir_ctx *synced_dir_context_new(void);

/*
 * @brief Deletes a Synchronized directory creator context.
 */
void synced_dir_context_delete(struct synceddir_ctx *ctx);

/*
 * @brief Register a Directory creator into the synchronized system, and
 * retrieve a flag to know whether the thread registered is now responsible for
 * creation and notification of the given directory or not. If responsible, the
 * directory creator is responsible for the whole access path including the
 * directory it wants to create.
 *
 * @param path              The path of the directory to be created.
 * @param is_responsible    The pointer on the flag to be filled in order to
 *                          indicate whether the caller becomes responsible for
 *                          the directory's creation or not.
 *
 * @return EXIT_SUCCESS     The creator was registered properly
 *         EXIT_FAILURE     The creator could not be registered properly
 */
int synced_dir_register(struct synceddir_ctx *ctx,
                        const char *path,
                        struct synceddir **sdirp, bool *is_responsiblep);

/*
 * @brief Unregister a registered directory creator. This means that either it
 * created the directory and notified other creators, or that it was notified
 * by another creator about the completion of the operation. It also notifies
 * non-responsible synchronized directory creators when the responsible one
 * unregisters.
 *
 * @param sdir              The synchronized directory descriptor to unregister from
 * @param is_responsible    Whether the unregistering user was responsible
 * @param exists            Whether the directory now exists or not.
 *
 */
void synced_dir_unregister(struct synceddir *sdir, bool is_responsible, bool exists);

/*
 * @brief Must be called by a non-responsible directory creator.Wait for the
 * responsible directory creator to complete its task whether it's a success or
 * a failure.
 *
 * @param sdir  The synchronized directory descriptor to unregister from
 *
 * @return true     The operation succeeded, the directory now exists.
 *         false    The operation failed, the creator must cascade the failure.
 */
bool synced_dir_completion_wait(struct synceddir *sdir);


#endif /* ! __CLOUDMIG_SYNCED_DIR_H__ */
