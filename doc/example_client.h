/* AUTOGENERATED FILE -- DO NOT EDIT */

/**
 * @file
 * @brief Client stubs for the example protocol
 * @addtogroup example
 * @{
 * @defgroup client Client Stubs
 * @{
 */

#ifndef EXAMPLE_CLIENT_H
#define EXAMPLE_CLIENT_H

#include "example_minirpc.h"

/**
 * @brief The role definition for the example client
 */
extern struct mrpc_protocol example_client;

mrpc_status_t example_ChooseColor(struct mrpc_connection *conn,
			ColorChoice *in);
mrpc_status_t example_GetNumColors(struct mrpc_connection *conn, Count **out);

typedef void (example_ChooseColor_callback_fn)(void *conn_private,
			void *msg_private, struct mrpc_message *msg,
			mrpc_status_t status);
typedef void (example_GetNumColors_callback_fn)(void *conn_private,
			void *msg_private, struct mrpc_message *msg,
			mrpc_status_t status, Count *reply);

mrpc_status_t example_ChooseColor_async(struct mrpc_connection *conn,
			example_ChooseColor_callback_fn *callback,
			void *private, ColorChoice *in);
mrpc_status_t example_GetNumColors_async(struct mrpc_connection *conn,
			example_GetNumColors_callback_fn *callback,
			void *private);

mrpc_status_t example_CrayonSelected(struct mrpc_connection *conn, Color *in);

/**
 * @}
 * @}
 */

#endif