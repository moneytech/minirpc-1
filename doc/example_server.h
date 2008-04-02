/* AUTOGENERATED FILE -- DO NOT EDIT */

/**
 * @file
 * @brief Server stubs for the example protocol
 * @addtogroup example_server
 * @{
 */

#ifndef EXAMPLE_SERVER_H
#define EXAMPLE_SERVER_H

#include "example_minirpc.h"

/**
 * @brief The role definition for the example server
 */
extern const struct mrpc_protocol * const example_server;

/**
 * @brief Pointers to handlers for messages received from the remote system
 *
 * If a message's handler is set to NULL, the message will be dropped and
 * (if possible) ::MINIRPC_PROCEDURE_UNAVAIL will be returned to the sender.
 */
struct example_server_operations {
	/**
	 * @brief Handler for @c choose_color procedure
	 * @param	conn_data
	 *	The cookie for the originating connection
	 * @param	msg
	 *	An opaque handle to the request message
	 * @param	in
	 *	The procedure parameter received from the remote system
	 * @return An error code, 0 for success, or ::MINIRPC_PENDING to
	 *		defer the reply
	 *
	 * If this function returns a result other than ::MINIRPC_PENDING,
	 * that result will be returned to the remote system immediately.
	 * If the handler returns ::MINIRPC_PENDING, no immediate reply
	 * will be made; the application can then call
	 * example_choose_color_send_async_reply() at its convenience to
	 * return MINIRPC_OK to the remote system, or
	 * example_choose_color_send_async_reply_error() to return
	 * a non-zero error code.
	 *
	 * @c in will be freed by miniRPC once the handler returns.  This
	 * will occur whether or not the handler returns ::MINIRPC_PENDING.
	 */
	mrpc_status_t (*choose_color)(void *conn_data,
			struct mrpc_message *msg, example_color_choice *in);

	/**
	 * @brief Handler for @c get_num_colors procedure
	 * @param	conn_data
	 *	The cookie for the originating connection
	 * @param	msg
	 *	An opaque handle to the request message
	 * @param[out]	out
	 *	The procedure parameter to be returned to the remote system
	 * @return An error code, 0 for success, or ::MINIRPC_PENDING to
	 *		defer the reply
	 *
	 * If this function returns ::MINIRPC_OK, that status code and
	 * the contents of @c out will be returned to the remote system
	 * immediately.  If it returns a non-zero error code other than
	 * ::MINIRPC_PENDING, that status code will be returned to the
	 * remote system and the contents of @c out will be discarded.
	 * If it returns ::MINIRPC_PENDING, no immediate reply will be made.
	 * At its convenience, the application can then call
	 * example_get_num_colors_send_async_reply() to return ::MINIRPC_OK
	 * and an example_count structure, or
	 * example_get_num_colors_send_async_reply_error() to return an error
	 * code.
	 *
	 * @c out is pre-allocated and should be filled in by the handler.
	 * It will be freed once the handler returns, even if the handler
	 * returns ::MINIRPC_PENDING; the application will need to allocate
	 * its own example_count if it later wants to send an asynchronous
	 * reply.
	 */
	mrpc_status_t (*get_num_colors)(void *conn_data,
			struct mrpc_message *msg, example_count *out);

	/**
	 * @brief Handler for @c crayon_selected message
	 * @param	conn_data
	 *	The cookie for the originating connection
	 * @param	msg
	 *	An opaque handle to the request message
	 * @param	in
	 *	The procedure parameter received from the remote system
	 *
	 * This is a handler for a message declared in the "msgs" section
	 * of the protocol definition file; it does not return any information
	 * to the remote system.  @c in will be freed once the handler returns.
	 */
	void (*crayon_selected)(void *conn_data, struct mrpc_message *msg,
			example_color *in);
};

/**
 * @brief Set the message handlers to be used by a connection
 * @param	conn
 *	The connection
 * @param	ops
 *	The operations structure to associate with the connection
 *
 * This function can be used to change the handlers which will be called when
 * a message arrives on the given connection.  It @em must be called from the
 * accept function, to set an initial set of handlers for an incoming
 * connection.  It can also be called at any point thereafter, including from
 * an event handler.
 *
 * The operations structure must not be modified or freed while it is
 * associated with one or more connections.
 */
int example_server_set_operations(struct mrpc_connection *conn,
			const struct example_server_operations *ops);

/**
 * @brief Send an asynchronous reply to a @c choose_color procedure call
 * @param	request
 *	The message handle originally passed to the @c choose_color method
 *
 * Return a ::MINIRPC_OK reply to the remote system for the specified request.
 * This is used to complete a request whose handler returned
 * ::MINIRPC_PENDING.
 */
mrpc_status_t example_choose_color_send_async_reply(struct mrpc_message *request);

/**
 * @brief Send an asynchronous reply to a @c get_num_colors procedure call
 * @param	request
 *	The message handle originally passed to the @c get_num_colors method
 * @param	out
 *	The reply structure to return
 * @sa free_example_count()
 *
 * Return a ::MINIRPC_OK reply and the contents of @c out to the remote system
 * for the specified request.  This is used to complete a request whose
 * handler returned ::MINIRPC_PENDING.  @c out must be allocated by the
 * caller, and may be freed by the caller after the function returns.
 */
mrpc_status_t example_get_num_colors_send_async_reply(struct mrpc_message *request,
			example_count *out);

/**
 * @brief Send an asynchronous error reply to a @c choose_color procedure call
 * @param	request
 *	The message handle originally passed to the @c choose_color method
 * @param	status
 *	The error code to return
 *
 * Return an error reply to the remote system for the specified request.
 * This is used to return an error for a request whose handler returned
 * ::MINIRPC_PENDING.
 */
mrpc_status_t example_choose_color_send_async_reply_error(struct mrpc_message *request,
			mrpc_status_t status);

/**
 * @brief Send an asynchronous error reply to a @c get_num_colors procedure call
 * @param	request
 *	The message handle originally passed to the @c get_num_colors method
 * @param	status
 *	The error code to return
 *
 * Return an error reply to the remote system for the specified request.
 * This is used to return an error for a request whose handler returned
 * ::MINIRPC_PENDING.
 */
mrpc_status_t example_get_num_colors_send_async_reply_error(struct mrpc_message *request,
			mrpc_status_t status);

/**
 * @}
 */

#endif
