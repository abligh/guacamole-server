/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is guacd.
 *
 * The Initial Developer of the Original Code is
 * Michael Jumper.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * David PHAM-VAN <d.pham-van@ulteo.com> Ulteo SAS - http://www.ulteo.com
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include <errno.h>
#include <syslog.h>
#include <libgen.h>

#include <guacamole/client.h>
#include <guacamole/error.h>

#include "client.h"
#include "log.h"

/* XML */
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/tree.h>
#include <libxml2/libxml/xpath.h>
#include <libxml2/libxml/xpathInternals.h>

void xml_init () {
    /* check the version. This calls xmlInitParser() */
    /* braces to stop indent getting confused */
    {LIBXML_TEST_VERSION}
}

void xml_deinit () {
    xmlCleanupParser ();
}

xmlNodePtr xml_get_node (xmlDoc * pDoc, const char *xpathexpr) {
    xmlChar *xpath_expr = (xmlChar *) xpathexpr;
    xmlXPathContextPtr xpathCtx = NULL;
    xmlXPathObjectPtr xpathObj = NULL;
    xmlNodeSetPtr nodeSet = NULL;
    int size;
    xmlNodePtr myNode = NULL;

    /* Create xpath evaluation context */
    if (NULL == (xpathCtx = xmlXPathNewContext (pDoc)))
        return NULL;
        
    /* Evaluate xpath expression */
    if (NULL == (xpathObj = xmlXPathEvalExpression (xpath_expr, xpathCtx))) {
        xmlXPathFreeContext (xpathCtx);
        return NULL;
    }

    nodeSet = xpathObj->nodesetval;
    size = (nodeSet) ? nodeSet->nodeNr : 0;
    if (size == 1)
        myNode = nodeSet->nodeTab[0];

    xmlXPathFreeObject (xpathObj);
    xmlXPathFreeContext (xpathCtx);
    return myNode;
}

char * xml_get_string (xmlDoc * pDoc, char *xpathexpr) {
    xmlNodePtr config_node = NULL;
    xmlChar *propval = NULL;
    
    /* Find the node in question beneath the config node */
    if (NULL == (config_node = xml_get_node (pDoc, xpathexpr)))
        return NULL;
    
    /* Find the property attached to that node; if it's not there, return 0 */
    if (NULL == (propval = xmlNodeGetContent (config_node)))
        return NULL;

    /* We would like to just return propval here, but that's an xmlChar * allocated by                                                                                                                    
     * libxml, and thus the caller can't just free() it - it would need to be xmlFree()'d.                                                                                                                
     * so we'll fiddle around and generate our own copy allocated with libc                                                                                                                               
     */
    char *value = strdup ((char *) propval);
    xmlFree (propval);            /* as xmlGetProp makes a copy of the string */
    return value;                 /* caller's responsibility to free() this */
}

void guacd_handle_connection(int fd) {

    guac_client* client;
    guac_client_plugin* plugin;
    guac_instruction* select;
    guac_instruction* connect;

    /* Open guac_socket */
    guac_socket* socket = guac_socket_open(fd);

    /* Get protocol from select instruction */
    select = guac_protocol_expect_instruction(
            socket, GUACD_USEC_TIMEOUT, "select");
    if (select == NULL) {

        /* Log error */
        guacd_log_guac_error("Error reading \"select\"");

        /* Free resources */
        guac_socket_close(socket);
        return;
    }

    /* Validate args to select */
    if (select->argc != 1) {

        /* Log error */
        guacd_log_error("Bad number of arguments to \"select\" (%i)",
                select->argc);

        /* Free resources */
        guac_socket_close(socket);
        return;
    }

    guacd_log_info("Protocol \"%s\" selected", select->argv[0]);

    /* Get plugin from protocol in select */
    plugin = guac_client_plugin_open(select->argv[0]);
    guac_instruction_free(select);

    if (plugin == NULL) {

        /* Log error */
        guacd_log_guac_error("Error loading client plugin");

        /* Free resources */
        guac_socket_close(socket);
        return;
    }

    /* Send args response */
    if (guac_protocol_send_args(socket, plugin->args)
            || guac_socket_flush(socket)) {

        /* Log error */
        guacd_log_guac_error("Error sending \"args\"");

        if (guac_client_plugin_close(plugin))
            guacd_log_guac_error("Error closing client plugin");

        guac_socket_close(socket);
        return;
    }

    /* Get args from connect instruction */
    connect = guac_protocol_expect_instruction(
            socket, GUACD_USEC_TIMEOUT, "connect");
    if (connect == NULL) {

        /* Log error */
        guacd_log_guac_error("Error reading \"connect\"");

        if (guac_client_plugin_close(plugin))
            guacd_log_guac_error("Error closing client plugin");

        guac_socket_close(socket);
        return;
    }

    /* Load and init client */
    client = guac_client_plugin_get_client(plugin, socket,
            connect->argc, connect->argv,
            guacd_client_log_info, guacd_client_log_error);

    guac_instruction_free(connect);

    if (client == NULL) {

        guacd_log_guac_error("Error instantiating client");

        if (guac_client_plugin_close(plugin))
            guacd_log_guac_error("Error closing client plugin");

        guac_socket_close(socket);
        return;
    }

    /* Start client threads */
    guacd_log_info("Starting client");
    if (guacd_client_start(client))
        guacd_log_error("Client finished abnormally");
    else
        guacd_log_info("Client finished normally");

    /* Clean up */
    guac_client_free(client);
    if (guac_client_plugin_close(plugin))
        guacd_log_error("Error closing client plugin");

    /* Close socket */
    guac_socket_close(socket);

    return;

}

void guacd_handle_connection_xml(int fd, char* xmlconfig) {

    guac_client* client = NULL;
    guac_client_plugin* plugin = NULL;
    char ** protocol_argv = NULL;
    int protocol_argc = 0;
    xmlDoc * pDoc = NULL;
    char * protocol = NULL;
    guac_socket* socket = NULL;

    if (NULL == (socket = guac_socket_open(fd))) {
        guacd_log_guac_error("Could not open socket");
        goto error;
    }

    if (NULL == (pDoc = xmlParseMemory (xmlconfig, strlen(xmlconfig)))) {
        guacd_log_guac_error("Could not parse XML");
        goto error;
    }

    if (NULL == (protocol = xml_get_string(pDoc, "/params/protocol"))) {
        guacd_log_guac_error("Could not find protocol element in XML");
        goto error;
    }

    guacd_log_info("Opening protocol '%s'", protocol);

    /* Get plugin from protocol in select */
    if (NULL == (plugin = guac_client_plugin_open(protocol))) {
        guacd_log_guac_error("Error loading client plugin");
        goto error;
    }

    /* Now parse protocol strings */
    const char ** arg;
    const char * params = "/params/";
    int lparams = strlen(params);
    for (arg = plugin->args; *arg && **arg; arg++)
        protocol_argc++;
    if (NULL == (protocol_argv = calloc(sizeof(char *), protocol_argc+1))) {
        guacd_log_guac_error("Cannot allocate protocol arguments");
        goto error;
    }

    int i;
    for (i=0; i<protocol_argc; i++) {
        const char * p;
        char * q;
        int l = strlen(plugin->args[i]);
        char * argname = malloc(lparams+l+1);
        if (!argname) {
            guacd_log_guac_error("Error duplicating argument list");
            goto error;
        }
        strncpy(argname, params, lparams);
        /* replace non-alpha characters by '_' for XML */
        for (p = plugin->args[i], q = argname+lparams; *p; p++, q++)
            *q = isalnum(*p)?*p:'_';
        *q='\0';
        char * value = xml_get_string(pDoc, argname);
        if (!value)
            value = strdup("");
	/* Don't do this as it logs passwords */
        /* guacd_log_info("Argument '%s' set to '%s'", plugin->args[i], value); */
        protocol_argv[i]=value;
    }

    guacd_log_info("Starting protocol %s, %d arguments", protocol, protocol_argc);

    /* Load and init client */
    if (NULL == (client = guac_client_plugin_get_client(plugin, socket,
                                                        protocol_argc, protocol_argv,
                                                        guacd_client_log_info, guacd_client_log_error))) {
        guacd_log_guac_error("Error instantiating client");
        goto error;
    }

    /* Start client threads */
    guacd_log_info("Starting client");
    if (guacd_client_start(client))
        guacd_log_error("Client finished abnormally");
    else
        guacd_log_info("Client finished normally");

  error:
    /* Clean up */
    if (client)
        guac_client_free(client);

    if (plugin && guac_client_plugin_close(plugin))
        guacd_log_error("Error closing client plugin");

    if (protocol_argv) {
        char **parg;
        for (parg = protocol_argv ; *parg; parg++)
            free(*parg);
        free(protocol_argv);
    }
    if (pDoc)
        xmlFreeDoc(pDoc);
    if (protocol)
        free (protocol);
    if (socket)
        guac_socket_close(socket);

    return;
}

void daemonize () {
    const char *devnull = "/dev/null";

    /* Fork once to ensure we aren't the process group leader */
    int i = fork ();
    if (i < 0) {
        fprintf (stderr, "Unable to fork\n");
        _exit (1);
    }

    /* Exit if we are the parent */
    if (i > 0)
       _exit (0);                  /* parent exits */

    /* Start a new session */
    setsid ();

    /* Fork again so the session group leader exits */
    i = fork ();
    if (i < 0) {
        fprintf (stderr, "Unable to fork\n");
        _exit (1);
    }

    /* Exit if we are the parent */
    if (i > 0)
       _exit (0);                  /* parent exits */

    if (chdir ("/") <0) {
        fprintf (stderr, "Unable to chdir /\n");
        _exit (1);
    }

    /* Now close all FDs and reopen the 3 stdxxx to /dev/null */
    for (i = getdtablesize () - 1; i >= 0; i--)
        close (i);

    i = open (devnull, O_RDWR);
    if (i == -1) {
        fprintf (stderr, "Unable to open /dev/null\n");
        _exit (1);
    }
    i = open (devnull, O_RDONLY);
    if (i != 0) {
        dup2 (i, 0);
        close (i);
    }
    i = open (devnull, O_WRONLY);
    if (i != 1) {
        dup2 (i, 1);
        close (i);
    }
    i = open (devnull, O_WRONLY);
    if (i != 2) {
        dup2 (i, 2);
        close (i);
    }
}



int main(int argc, char* argv[]) {

    /* Server */
    int socket_fd;
    struct addrinfo* addresses;
    struct addrinfo* current_address;
    char bound_address[1024];
    char bound_port[64];
    int opt_on = 1;

    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP
    };

    /* Client */
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    int connected_socket_fd;

    /* Arguments */
    char* listen_address = NULL; /* Default address of INADDR_ANY */
    char* listen_port = "4822";  /* Default port */
    char* pidfile = NULL;
    int opt;
    int foreground = 0;
    int suppliedfd = -1;
    char * xmlconfig = NULL;

    /* General */
    int retval;

    xml_init();

    /* Parse arguments */
    while ((opt = getopt(argc, argv, "l:b:p:x:s:f")) != -1) {
        if (opt == 'l') {
            listen_port = strdup(optarg);
        }
        else if (opt == 'b') {
            listen_address = strdup(optarg);
        }
        else if (opt == 'f') {
            foreground++;
        }
        else if (opt == 'p') {
            pidfile = strdup(optarg);
        }
        else if (opt == 's') {
            suppliedfd = atoi(optarg);
            foreground = 2;
        }
        else if (opt == 'x') {
            xmlconfig = strdup (optarg);
        }
        else {

            fprintf(stderr, "USAGE: %s"
                    " [-l LISTENPORT]"
                    " [-b LISTENADDRESS]"
                    " [-p PIDFILE]"
		    " [-s SOCKETFD]"
                    " [-f]"
                    " [-x XMLCONFIG]\n", argv[0]);

            exit(EXIT_FAILURE);
        }
    }

    /* Daemonize before we start opening sockets, as this closes FDs */
    if (!foreground)
        daemonize();

    if (pidfile != NULL) {
        /* Attempt to open pidfile and write PID */
        FILE* pidf = fopen(pidfile, "w");
        if (pidf) {
            fprintf(pidf, "%d\n", getpid());
            fclose(pidf);
        } else {
            /* Warn on failure */
            guacd_log_error("Could not write PID file: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    /* Set up logging prefix */
    strncpy(log_prefix, basename(argv[0]), sizeof(log_prefix));

    /* Open log as early as we can */
    openlog(NULL, LOG_PID, LOG_DAEMON);

    /* Ignore SIGPIPE */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        guacd_log_info("Could not set handler for SIGPIPE to ignore. SIGPIPE may cause termination of the daemon.");
    }

    /* Ignore SIGCHLD (force automatic removal of children) */
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        guacd_log_info("Could not set handler for SIGCHLD to ignore. Child processes may pile up in the process table.");
    }

    /* Handle the case where we have a supplied fd */
    if (suppliedfd != -1) {
        if (xmlconfig)
            guacd_handle_connection_xml(suppliedfd, xmlconfig);
        else
            guacd_handle_connection(suppliedfd);
        goto exit;
    }

    /* Get addresses for binding */
    if ((retval = getaddrinfo(listen_address, listen_port, &hints, &addresses))) {
        guacd_log_error("Error parsing given address or port: %s",
                        gai_strerror(retval));
        exit(EXIT_FAILURE);
    }
    
    /* Get socket */
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        guacd_log_error("Error opening socket: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    /* Allow socket reuse */
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (void*) &opt_on, sizeof(opt_on))) {
        guacd_log_info("Unable to set socket options for reuse: %s", strerror(errno));
    }

    /* Attempt binding of each address until success */
    current_address = addresses;
    while (current_address != NULL) {

        int retval;

        /* Resolve hostname */
        if ((retval = getnameinfo(current_address->ai_addr,
                current_address->ai_addrlen,
                bound_address, sizeof(bound_address),
                bound_port, sizeof(bound_port),
                NI_NUMERICHOST | NI_NUMERICSERV)))
            guacd_log_error("Unable to resolve host: %s",
                    gai_strerror(retval));

        /* Attempt to bind socket to address */
        if (bind(socket_fd,
                    current_address->ai_addr,
                    current_address->ai_addrlen) == 0) {

            guacd_log_info("Successfully bound socket to "
                    "host %s, port %s", bound_address, bound_port);

            /* Done if successful bind */
            break;

        }

        /* Otherwise log information regarding bind failure */
        else
            guacd_log_info("Unable to bind socket to "
                    "host %s, port %s: %s",
                    bound_address, bound_port, strerror(errno));

        current_address = current_address->ai_next;

    }

    /* If unable to bind to anything, fail */
    if (current_address == NULL) {
        guacd_log_error("Unable to bind socket to any addresses.");
        exit(EXIT_FAILURE);
    }

    /* Log listening status */
    syslog(LOG_INFO,
            "Listening on host %s, port %s", bound_address, bound_port);

    /* Free addresses */
    freeaddrinfo(addresses);

    /* Daemon loop */
    for (;;) {

        pid_t child_pid;

        /* Listen for connections */
        if (listen(socket_fd, 5) < 0) {
            guacd_log_error("Could not listen on socket: %s", strerror(errno));
            return 3;
        }

        /* Accept connection */
        client_addr_len = sizeof(client_addr);
        connected_socket_fd = accept(socket_fd, (struct sockaddr*) &client_addr, &client_addr_len);
        if (connected_socket_fd < 0) {
            guacd_log_error("Could not accept client connection: %s", strerror(errno));
            return 3;
        }

        /* 
         * Once connection is accepted, send child into background.
         *
         * Note that we prefer fork() over threads for connection-handling
         * processes as they give each connection its own memory area, and
         * isolate the main daemon and other connections from errors in any
         * particular client plugin.
         */

        child_pid = (foreground>1)?0:fork();

        /* If error, log */
        if (child_pid == -1)
            guacd_log_error("Error forking child process: %s", strerror(errno));

        /* If child, start client, and exit when finished */
        else if (child_pid == 0) {
            if (xmlconfig)
                guacd_handle_connection_xml(connected_socket_fd, xmlconfig);
            else
                guacd_handle_connection(connected_socket_fd);
            close(connected_socket_fd);
            return 0;
        }

        /* If parent, close reference to child's descriptor */
        else if (close(connected_socket_fd) < 0) {
            guacd_log_error("Error closing daemon reference to child descriptor: %s", strerror(errno));
        }

    }

    /* Close socket */
    if (close(socket_fd) < 0) {
        guacd_log_error("Could not close socket: %s", strerror(errno));
        return 3;
    }

  exit:
    xml_deinit();
    return 0;

}

