#ifndef PacketHandler_H
#define PacketHandler_H

class Measurement
{
private:
    /* data */
public:
    //Measurement(/* args */);
    //~Measurement();
    int init(char **list);
    void cleanup();
private:
    void generateRandomPort();
    void generateIPAddresses();
};

#endif