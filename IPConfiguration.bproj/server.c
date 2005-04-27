/*
 * Copyright (c) 2000 - 2004 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <servers/bootstrap.h>
#include <syslog.h>
#include <CoreFoundation/CFMachPort.h>

#include "dprintf.h"
#include "ipconfig.h"
#include "ipconfigd.h"
#include "ipconfig_ext.h"
#include "globals.h"

static uid_t S_uid = -1;
static gid_t S_gid = -1;

static __inline__ void
read_trailer(mig_reply_error_t * request)
{
    mach_msg_format_0_trailer_t	*trailer;
    trailer = (mach_msg_security_trailer_t *)((vm_offset_t)request +
					      round_msg(request->Head.msgh_size));

    if ((trailer->msgh_trailer_type == MACH_MSG_TRAILER_FORMAT_0) &&
	(trailer->msgh_trailer_size >= MACH_MSG_TRAILER_FORMAT_0_SIZE)) {
	S_uid = trailer->msgh_sender.val[0];
	S_gid = trailer->msgh_sender.val[1];
    }
    else {
	S_uid = -1;
	S_gid = -1;
    }
}

kern_return_t
_ipconfig_config_if(port_t p, if_name_t name)
{
    dprintf(("config called with %s\n", name));
    return (KERN_SUCCESS);
}

kern_return_t
_ipconfig_config_all(port_t p)
{
    dprintf(("config all called\n"));
    return (KERN_SUCCESS);
}

kern_return_t
_ipconfig_wait_if(port_t p, if_name_t name)
{
    dprintf(("Waiting for %s to complete\n", name));
    if (S_uid == 0 && wait_if(name) == TRUE)
	return (KERN_SUCCESS);
    return (KERN_FAILURE);
}

kern_return_t
_ipconfig_wait_all(port_t p)
{

    dprintf(("Waiting for all interfaces to complete\n"));
    if (S_uid == 0) {
	wait_all();
	return (KERN_SUCCESS);
    }
    return (KERN_FAILURE);
}

kern_return_t
_ipconfig_if_name(port_t p, int intface, if_name_t name)
{

    dprintf(("Getting interface name\n"));
    if (get_if_name(intface, name) == TRUE)
	return (KERN_SUCCESS);
    return (KERN_FAILURE);
}

kern_return_t
_ipconfig_if_addr(port_t p, if_name_t name, u_int32_t * addr)
{
    dprintf(("Getting interface address\n"));
    if (get_if_addr(name, addr) == TRUE)
	return (KERN_SUCCESS);
    return (KERN_FAILURE);
}

kern_return_t
_ipconfig_if_count(port_t p, int * count)
{
    dprintf(("Getting interface count\n"));
    *count = get_if_count();
    return (KERN_SUCCESS);
}

kern_return_t
_ipconfig_get_option(port_t p, if_name_t name, int option_code,
		     inline_data_t option_data,
		     unsigned int * option_dataCnt)
{
    if (get_if_option(name, option_code, option_data, option_dataCnt) 
	== TRUE) {
	return (KERN_SUCCESS);
    }
    return (KERN_FAILURE);
}

kern_return_t
_ipconfig_get_packet(port_t p, if_name_t name,
		     inline_data_t packet_data,
		     unsigned int * packet_dataCnt)
{
    if (get_if_packet(name, packet_data, packet_dataCnt) == TRUE) {
	return (KERN_SUCCESS);
    }
    return (KERN_FAILURE);
}

kern_return_t
_ipconfig_set(port_t p, if_name_t name,
	      ipconfig_method_t method,
	      inline_data_t method_data,
	      unsigned int method_data_len,
	      ipconfig_status_t * status)
{
    if (S_uid != 0) {
	*status = ipconfig_status_permission_denied_e;
    }
    else {
	*status = set_if(name, method, method_data, method_data_len, NULL);
    }
    return (KERN_SUCCESS);
}

kern_return_t
_ipconfig_set_verbose(port_t p, int verbose,
		      ipconfig_status_t * status)
{
    if (S_uid != 0) {
	*status = ipconfig_status_permission_denied_e;
    }
    else {
	*status = set_verbose(verbose);
    }
    return (KERN_SUCCESS);
}

#ifdef IPCONFIG_TEST_NO_ENTRY
kern_return_t
_ipconfig_set_something(port_t p, int verbose,
			ipconfig_status_t * status)
{
    return (KERN_SUCCESS);
}
#endif IPCONFIG_TEST_NO_ENTRY

boolean_t
server_active()
{
    boolean_t 		active = FALSE;
    port_t		server;
    kern_return_t	status;

    status = ipconfig_server_port(&server, &active);
    if (active != FALSE)
	return (TRUE);
    return (FALSE);
}

static void
S_ipconfig_server(CFMachPortRef port, void *msg, CFIndex size, void *info)
{
    char 		reply_buf[1024];
    mach_msg_options_t 	options = 0;
    mig_reply_error_t * request = (mig_reply_error_t *)msg;
    mig_reply_error_t *	reply = (mig_reply_error_t *)reply_buf;
    mach_msg_return_t 	r = MACH_MSG_SUCCESS;
    extern boolean_t 	ipconfig_server(mig_reply_error_t *, 
					mig_reply_error_t *);

    read_trailer(request);
    if (ipconfig_server(request, reply) == FALSE) {
	my_log(LOG_INFO, "IPConfiguration: unknown message ID (%d) received",
	       request->Head.msgh_id);
    }

    /* Copied from Libc/mach/mach_msg.c:mach_msg_server_once(): Start */
    if (!(reply->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX)) {
	if (reply->RetCode == MIG_NO_REPLY)
	    reply->Head.msgh_remote_port = MACH_PORT_NULL;
	else if ((reply->RetCode != KERN_SUCCESS) &&
		 (request->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX)) {
	    /* destroy the request - but not the reply port */
	    request->Head.msgh_remote_port = MACH_PORT_NULL;
	    mach_msg_destroy(&request->Head);
	}
    }
    /*
     *	We don't want to block indefinitely because the client
     *	isn't receiving messages from the reply port.
     *	If we have a send-once right for the reply port, then
     *	this isn't a concern because the send won't block.
     *	If we have a send right, we need to use MACH_SEND_TIMEOUT.
     *	To avoid falling off the kernel's fast RPC path unnecessarily,
     *	we only supply MACH_SEND_TIMEOUT when absolutely necessary.
     */
    if (reply->Head.msgh_remote_port != MACH_PORT_NULL) {
	r = mach_msg(&reply->Head,
		     (MACH_MSGH_BITS_REMOTE(reply->Head.msgh_bits) ==
		      MACH_MSG_TYPE_MOVE_SEND_ONCE) ?
		     MACH_SEND_MSG|options :
		     MACH_SEND_MSG|MACH_SEND_TIMEOUT|options,
		     reply->Head.msgh_size, 0, MACH_PORT_NULL,
		     MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
	if ((r != MACH_SEND_INVALID_DEST) &&
	    (r != MACH_SEND_TIMED_OUT))
	    goto done_once;
	r = MACH_MSG_SUCCESS;
    }
    if (reply->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX)
	mach_msg_destroy(&reply->Head);
 done_once:
    /* Copied from Libc/mach/mach_msg.c:mach_msg_server_once(): End */

    if (r != MACH_MSG_SUCCESS) {
	my_log(LOG_INFO, "IPConfiguration msg_send: %s", mach_error_string(r));
    }
    return;
}

void
server_init()
{
    boolean_t		active;
    CFRunLoopSourceRef	rls;
    CFMachPortRef	ipconfigd_port;
    kern_return_t 	status;

    active = FALSE;
    status = bootstrap_status(bootstrap_port, IPCONFIG_SERVER, &active);

    switch (status) {
      case BOOTSTRAP_SUCCESS :
	  if (active) {
	      fprintf(stderr, "\"%s\" is currently active.\n", 
		     IPCONFIG_SERVER);
	      return;
	  }
	  break;
      case BOOTSTRAP_UNKNOWN_SERVICE :
	  break;
      default :
	  fprintf(stderr,
		 "bootstrap_status(): %s\n", mach_error_string(status));
	  return;
	  break;
    }

    ipconfigd_port = CFMachPortCreate(NULL, S_ipconfig_server, NULL, NULL);
    rls = CFMachPortCreateRunLoopSource(NULL, ipconfigd_port, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), rls, kCFRunLoopDefaultMode);
    CFRelease(rls);

    status = bootstrap_register(bootstrap_port, IPCONFIG_SERVER, 
				CFMachPortGetPort(ipconfigd_port));
    if (status != BOOTSTRAP_SUCCESS) {
	mach_error("bootstrap_register", status);
    }
    return;
}

