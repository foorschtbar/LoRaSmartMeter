#ifndef webcontent_h
#define webcontent_h

const char *serverIndex =
    "<html>"
    "<head>"
    "<title>LoRaSmartMeter</title>"
    "</head>"
    "<body>"
    "<form method='POST' action='/update' enctype='multipart/form-data' id='upload_form'>"
    "<input type='file' name='update'>"
    "<input type='submit' value='Update'>"
    "</form>"
    "</body>"
    "</html>";

#endif
