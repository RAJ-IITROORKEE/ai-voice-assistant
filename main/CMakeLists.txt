set(RELAY_EMBED_FILES)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/certs/relay_ca.pem")
  list(APPEND RELAY_EMBED_FILES "certs/relay_ca.pem")
endif()

idf_component_register(
  SRCS
    "main.cpp"
    "audio.cpp"
    "playback_policy.cpp"
    "relay_protocol.cpp"
    "relay_client.cpp"
  INCLUDE_DIRS
    "include"
  REQUIRES
    esp_event
    esp_netif
    esp-tls
    esp_wifi
    esp_websocket_client
    driver
    heap
    json
    lwip
    nvs_flash
  EMBED_TXTFILES
    ${RELAY_EMBED_FILES}
)
