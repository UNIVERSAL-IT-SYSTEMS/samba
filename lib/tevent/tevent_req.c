/*
   Unix SMB/CIFS implementation.
   Infrastructure for async requests
   Copyright (C) Volker Lendecke 2008
   Copyright (C) Stefan Metzmacher 2009

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "replace.h"
#include "tevent.h"
#include "tevent_internal.h"
#include "tevent_util.h"

/**
 * @brief Print an tevent_req structure in debug messages
 * @param[in] mem_ctx	The memory context for the result
 * @param[in] req	The request to be printed
 * @retval		Text representation of req
 *
 */

char *tevent_req_print(TALLOC_CTX *mem_ctx, struct tevent_req *req)
{
	return talloc_asprintf(mem_ctx,
			       "tevent_req[%p/%s]: state[%d] error[%lld (0x%llX)] "
			       " state[%s (%p)] timer[%p]",
			       req, req->internal.location,
			       req->internal.state,
			       (unsigned long long)req->internal.error,
			       (unsigned long long)req->internal.error,
			       talloc_get_name(req->private_state),
			       req->private_state,
			       req->internal.timer
			       );
}

/**
 * @brief Create an async request
 * @param[in] mem_ctx	The memory context for the result
 * @param[in] ev	The event context this async request will be driven by
 * @retval		A new async request
 *
 * The new async request will be initialized in state ASYNC_REQ_IN_PROGRESS
 */

struct tevent_req *_tevent_req_create(TALLOC_CTX *mem_ctx,
				    void *pstate,
				    size_t state_size,
				    const char *type,
				    const char *location)
{
	struct tevent_req *req;
	void **ppstate = (void **)pstate;
	void *state;

	req = talloc_zero(mem_ctx, struct tevent_req);
	if (req == NULL) {
		return NULL;
	}
	req->internal.private_type	= type;
	req->internal.location		= location;
	req->internal.state		= TEVENT_REQ_IN_PROGRESS;

	state = talloc_size(req, state_size);
	if (state == NULL) {
		talloc_free(req);
		return NULL;
	}
	talloc_set_name_const(state, type);

	req->private_state = state;

	*ppstate = state;
	return req;
}

static void tevent_req_finish(struct tevent_req *req, enum tevent_req_state state)
{
	req->internal.state = state;
	if (req->async.fn != NULL) {
		req->async.fn(req);
	}
}

/**
 * @brief An async request has successfully finished
 * @param[in] req	The finished request
 *
 * async_req_done is to be used by implementors of async requests. When a
 * request is successfully finished, this function calls the user's completion
 * function.
 */

void tevent_req_done(struct tevent_req *req)
{
	tevent_req_finish(req, TEVENT_REQ_DONE);
}

/**
 * @brief An async request has seen an error
 * @param[in] req	The request with an error
 * @param[in] error	The error code
 *
 * tevent_req_done is to be used by implementors of async requests. When a
 * request can not successfully completed, the implementation should call this
 * function with the appropriate status code.
 *
 * If error is 0 the function returns false and does nothing more.
 *
 * Call pattern would be
 * \code
 * int error = first_function();
 * if (tevent_req_error(req, error)) {
 *	return;
 * }
 *
 * error = second_function();
 * if (tevent_req_error(req, error)) {
 *	return;
 * }
 *
 * tevent_req_done(req);
 * return;
 * \endcode
 */

bool tevent_req_error(struct tevent_req *req, uint64_t error)
{
	if (error == 0) {
		return false;
	}

	req->internal.error = error;
	tevent_req_finish(req, TEVENT_REQ_USER_ERROR);
	return true;
}

/**
 * @brief Helper function for nomem check
 * @param[in] p		The pointer to be checked
 * @param[in] req	The request being processed
 *
 * Convenience helper to easily check alloc failure within a callback
 * implementing the next step of an async request.
 *
 * Call pattern would be
 * \code
 * p = talloc(mem_ctx, bla);
 * if (tevent_req_nomem(p, req)) {
 *	return;
 * }
 * \endcode
 */

bool tevent_req_nomem(const void *p, struct tevent_req *req)
{
	if (p != NULL) {
		return false;
	}
	tevent_req_finish(req, TEVENT_REQ_NO_MEMORY);
	return true;
}

/**
 * @brief Timed event callback
 * @param[in] ev	Event context
 * @param[in] te	The timed event
 * @param[in] now	zero time
 * @param[in] priv	The async request to be finished
 */
static void tevent_req_trigger(struct tevent_context *ev,
			       struct tevent_timer *te,
			       struct timeval zero,
			       void *private_data)
{
	struct tevent_req *req = talloc_get_type(private_data,
				 struct tevent_req);

	talloc_free(req->internal.trigger);
	req->internal.trigger = NULL;

	tevent_req_finish(req, req->internal.state);
}

/**
 * @brief Finish a request before the caller had the change to set the callback
 * @param[in] req	The finished request
 * @param[in] ev	The tevent_context for the timed event
 * @retval		On success req will be returned,
 * 			on failure req will be destroyed
 *
 * An implementation of an async request might find that it can either finish
 * the request without waiting for an external event, or it can't even start
 * the engine. To present the illusion of a callback to the user of the API,
 * the implementation can call this helper function which triggers an
 * immediate timed event. This way the caller can use the same calling
 * conventions, independent of whether the request was actually deferred.
 */

struct tevent_req *tevent_req_post(struct tevent_req *req,
				   struct tevent_context *ev)
{
	req->internal.trigger = tevent_add_timer(ev, req, ev_timeval_zero(),
						 tevent_req_trigger, req);
	if (!req->internal.trigger) {
		talloc_free(req);
		return NULL;
	}

	return req;
}

bool tevent_req_is_in_progress(struct tevent_req *req)
{
	if (req->internal.state == TEVENT_REQ_IN_PROGRESS) {
		return true;
	}

	return false;
}

bool tevent_req_is_error(struct tevent_req *req, enum tevent_req_state *state,
			uint64_t *error)
{
	if (req->internal.state == TEVENT_REQ_DONE) {
		return false;
	}
	if (req->internal.state == TEVENT_REQ_USER_ERROR) {
		*error = req->internal.error;
	}
	*state = req->internal.state;
	return true;
}

static void tevent_req_timedout(struct tevent_context *ev,
			       struct tevent_timer *te,
			       struct timeval now,
			       void *private_data)
{
	struct tevent_req *req = talloc_get_type(private_data,
				 struct tevent_req);

	talloc_free(req->internal.timer);
	req->internal.timer = NULL;

	tevent_req_finish(req, TEVENT_REQ_TIMED_OUT);
}

bool tevent_req_set_timeout(struct tevent_req *req,
			    struct tevent_context *ev,
			    struct timeval endtime)
{
	talloc_free(req->internal.timer);

	req->internal.timer = tevent_add_timer(ev, req, endtime,
					       tevent_req_timedout,
					       req);
	if (tevent_req_nomem(req->internal.timer, req)) {
		return false;
	}

	return true;
}
