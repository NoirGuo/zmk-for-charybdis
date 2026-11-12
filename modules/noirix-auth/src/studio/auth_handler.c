/*
 * Noirix device identity reporting.
 *
 * Registers the "noirix__auth" ZMK Studio RPC subsystem. The host asks the
 * device for its MCU unique ID and checks it against a whitelist of authorized
 * device IDs (stored as SHA-256 hashes on the host side). No shared secret or
 * MAC is involved.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pb_decode.h>
#include <pb_encode.h>
#include <zmk/studio/custom.h>
#include <zmk/auth/auth.pb.h>

#include <zephyr/kernel.h>
#include <string.h>

#if IS_ENABLED(CONFIG_HWINFO)
#include <zephyr/drivers/hwinfo.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define NOIRIX_AUTH_ID_SIZE 16u

static struct zmk_rpc_custom_subsystem_meta auth_meta = {
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

ZMK_RPC_CUSTOM_SUBSYSTEM(noirix__auth, &auth_meta, auth_rpc_handle_request);

ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(noirix__auth, zmk_auth_Response);

static bool auth_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                    pb_callback_t *encode_response);

static int handle_authenticate(zmk_auth_Response *resp) {
    zmk_auth_ErrorResponse err = zmk_auth_ErrorResponse_init_zero;

    uint8_t device_id[NOIRIX_AUTH_ID_SIZE] = {0};
    size_t id_len = 0;
#if IS_ENABLED(CONFIG_HWINFO)
    ssize_t n = hwinfo_get_device_id(device_id, sizeof(device_id));
    if (n > 0) {
        id_len = (size_t)n;
    }
#endif
    if (id_len == 0) {
        snprintf(err.message, sizeof(err.message), "device id unavailable");
        resp->which_response_type = zmk_auth_Response_error_tag;
        resp->response_type.error = err;
        return 0;
    }

    zmk_auth_AuthenticateResponse result = zmk_auth_AuthenticateResponse_init_zero;
    memcpy(result.device_id.bytes, device_id, id_len);
    result.device_id.size = id_len;

    resp->which_response_type = zmk_auth_Response_authenticate_tag;
    resp->response_type.authenticate = result;
    return 0;
}

static bool auth_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                    pb_callback_t *encode_response) {
    zmk_auth_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(noirix__auth, encode_response);

    zmk_auth_Request req = zmk_auth_Request_init_zero;

    pb_istream_t req_stream =
        pb_istream_from_buffer(raw_request->payload.bytes, raw_request->payload.size);
    if (!pb_decode(&req_stream, zmk_auth_Request_fields, &req)) {
        LOG_WRN("noirix-auth: failed to decode request: %s", PB_GET_ERROR(&req_stream));
        zmk_auth_ErrorResponse err = zmk_auth_ErrorResponse_init_zero;
        snprintf(err.message, sizeof(err.message), "failed to decode request");
        resp->which_response_type = zmk_auth_Response_error_tag;
        resp->response_type.error = err;
        return true;
    }

    int rc = -1;
    switch (req.which_request_type) {
    case zmk_auth_Request_authenticate_tag:
        rc = handle_authenticate(resp);
        break;
    default:
        LOG_WRN("noirix-auth: unsupported request type: %d", req.which_request_type);
    }

    if (rc != 0) {
        zmk_auth_ErrorResponse err = zmk_auth_ErrorResponse_init_zero;
        snprintf(err.message, sizeof(err.message), "failed to process request");
        resp->which_response_type = zmk_auth_Response_error_tag;
        resp->response_type.error = err;
    }
    return true;
}
