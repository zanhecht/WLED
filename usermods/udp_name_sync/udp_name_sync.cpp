#include "wled.h"

class UdpNameSync : public Usermod {

  private:

    bool enabled = false;
    char segmentName[WLED_MAX_SEGNAME_LEN] = {0};
    static const char _name[];
    static const char _enabled[];

  public:
    /**
     * Enable/Disable the usermod
     */
    inline void enable(bool value) { enabled = value; }

    /**
     * Get usermod enabled/disabled state
     */
    inline bool isEnabled() const { return enabled; }

    void setup() override {
    }

    void loop() override {
      if (!WLED_CONNECTED) return;
      if (!udpConnected) return;
      Segment& mainseg = strip.getMainSegment();
      if (!strlen(segmentName) && !mainseg.name) return; //name was never set, do nothing

      IPAddress broadcastIp = uint32_t(Network.localIP()) | ~uint32_t(Network.subnetMask());
      byte udpOut[WLED_MAX_SEGNAME_LEN + 2];
      udpOut[0] = 200; // custom usermod packet type (avoid 0..5 used by core protocols)  
      if (strlen(segmentName) && !mainseg.name) { // name cleared
        notifierUdp.beginPacket(broadcastIp, udpPort);
        segmentName[0] = '\0';
        DEBUG_PRINTLN(F("UdpNameSync: sending empty name"));
        udpOut[1] = 0; // explicit empty string
        notifierUdp.write(udpOut, 2);
        notifierUdp.endPacket();
        return;
      }

      const char* curName = mainseg.name ? mainseg.name : "";
      if (strncmp(curName, segmentName, sizeof(segmentName)) == 0) return; // same name, do nothing

      notifierUdp.beginPacket(broadcastIp, udpPort);
      DEBUG_PRINT(F("UdpNameSync: saving segment name "));
      DEBUG_PRINTLN(mainseg.name);
      strlcpy(segmentName, mainseg.name, sizeof(segmentName));
      strlcpy((char *)&udpOut[1], segmentName, sizeof(udpOut) - 1); // leave room for header byte
      notifierUdp.write(udpOut, 2 + strnlen((char *)&udpOut[1], sizeof(udpOut) - 1));
      notifierUdp.endPacket();
      DEBUG_PRINT(F("UdpNameSync: Sent segment name : "));
      DEBUG_PRINTLN(segmentName);
    }

    bool onUdpPacket(uint8_t * payload, size_t len) override {
      DEBUG_PRINT(F("UdpNameSync: Received packet"));
      if (receiveDirect) return false;
      if (payload[0] != 200) return false;
      //else
      Segment& mainseg = strip.getMainSegment();
      mainseg.setName((char *)&payload[1]);
      DEBUG_PRINT(F("UdpNameSync: set segment name"));
      return true;
    }
};


// add more strings here to reduce flash memory usage
const char UdpNameSync::_name[]    PROGMEM = "UdpNameSync";
const char UdpNameSync::_enabled[] PROGMEM = "enabled";

static UdpNameSync udp_name_sync;
REGISTER_USERMOD(udp_name_sync);
