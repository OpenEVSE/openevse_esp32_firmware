#ifndef WEB_SERVER_MDNS_H
#define WEB_SERVER_MDNS_H

#include <stdint.h>

/**
 * Publish the running listener's services and report each failed registration.
 * Publish OpenEVSE metadata only after its service was registered successfully.
 */
template <typename Responder, typename PublishMetadata, typename ReportFailure>
void web_server_publish_mdns(Responder &responder, uint16_t port,
                             PublishMetadata publish_metadata, ReportFailure report_failure)
{
  responder.addService("http", "tcp", port);
  responder.addService("openevse", "tcp", port);
  publish_metadata();
}

#endif // WEB_SERVER_MDNS_H
