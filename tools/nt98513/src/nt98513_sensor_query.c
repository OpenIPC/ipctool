/* SPDX-License-Identifier: MIT */
#include "nt98513_sensor_query.h"

enum nt98513_sensor_query_status nt98513_sensor_active_query(
    const struct nt98513_client_io *io,
    struct nt98513_sensor_active *selection,
    struct nt98513_sensor_query_diagnostic *diagnostic)
{
    const struct nt98513_client_request request = {
        NT98513_SEND_COMMAND, "usys -sensor_active 0", NULL, 0
    };
    struct nt98513_sensor_active parsed;
    enum nt98513_client_status status;

    if (io == NULL || selection == NULL || diagnostic == NULL ||
        io->address == NULL || io->open == NULL ||
        io->ioctl == NULL || io->close == NULL)
        return NT98513_SENSOR_QUERY_ARGUMENT;

    diagnostic->parse_called = 0;
    diagnostic->parser_status = NT98513_CONTROL_ARGUMENT;
    status = nt98513_client_run(&request, io, &diagnostic->client);
    /* The actual client includes close in success and preserves raw failures.
     * Never parse or publish a reply from an unsuccessful transaction. */
    if (status != NT98513_CLIENT_OK || diagnostic->client.ioctl_return != 0 ||
        !diagnostic->client.remote_valid || diagnostic->client.reply.remote_status != 0 ||
        !diagnostic->client.response_terminated ||
        diagnostic->client.response_size >= sizeof(diagnostic->client.response))
        return NT98513_SENSOR_QUERY_CLIENT_ERROR;

    diagnostic->parse_called = 1;
    diagnostic->parser_status = nt98513_sensor_active_parse(
        diagnostic->client.response, diagnostic->client.response_size + 1, &parsed);
    if (diagnostic->parser_status != NT98513_CONTROL_OK)
        return NT98513_SENSOR_QUERY_REPLY_ERROR;

    *selection = parsed;
    return parsed.selection == NT98513_SENSOR_OFF ?
        NT98513_SENSOR_QUERY_NOT_SELECTED : NT98513_SENSOR_QUERY_OK;
}
