#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define DATA_TIMER  2000

struct FRAME { 
    unsigned char kind; /* FRAME_DATA */
    unsigned char ack;
    unsigned char seq;
    unsigned char data[PKT_LEN]; 
    unsigned int  padding;       //补一位对齐！从259补到260，总大小会变成264
};

static unsigned char frame_nr = 0, buffer[PKT_LEN], nbuffered;
static unsigned char frame_expected = 0;
static int phl_ready = 0;

static void put_frame(unsigned char *frame, int len)
{
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;  //为防止该帧没有全部上传完成，下一个可发送帧就已经到达从而产生错误。该帧传送完毕后，如果物理层有足够空间将会自动将其置为1
}

static void send_data_frame(void)
{
    struct FRAME s;

    s.kind = FRAME_DATA;
    s.seq = frame_nr;
    s.ack = 1 - frame_expected;
    memcpy(s.data, buffer, PKT_LEN);

    dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);

    put_frame((unsigned char *)&s, 3 + PKT_LEN);    //包括帧头前三个数据元素在内的总长度
    start_timer(frame_nr, DATA_TIMER);              //在物理层发完当前总内容以后才会开启，因此它记录了Tf，发送时延
}

static void send_ack_frame(void)
{
    struct FRAME s;

    s.kind = FRAME_ACK;
    s.ack = 1 - frame_expected;

    dbg_frame("Send ACK  %d\n", s.ack);

    put_frame((unsigned char *)&s, 2);
}

int main(int argc, char **argv)
{
    int event, arg;
    struct FRAME f;
    int len = 0;

    protocol_init(argc, argv); 
    lprintf("Designed by Jiang Yanjun, build: " __DATE__"  "__TIME__"\n");

    disable_network_layer();

    for (;;) {
        event = wait_for_event(&arg);

        switch (event) {
        case NETWORK_LAYER_READY:
            get_packet(buffer);
            nbuffered++;
            send_data_frame();
            break;

        case PHYSICAL_LAYER_READY:
            phl_ready = 1;
            break;

        case FRAME_RECEIVED: 
            len = recv_frame((unsigned char *)&f, sizeof f);        //len=263,实际上最后的五个字节中校验位只用了前四个，因此得到有用信息的位数实际上是263而非FRAME结构体真正大小264
            if (len < 5 || crc32((unsigned char *)&f, len) != 0) {  //尚不清楚<5判断的用意
                dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                break;
            }
            if (f.kind == FRAME_ACK) 
                dbg_frame("Recv ACK  %d\n", f.ack);
            if (f.kind == FRAME_DATA) {
                dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);
                if (f.seq == frame_expected) {
                    put_packet(f.data, len - 7);            //减去校验位4位和剩下三个信息，只留数据部分，-7
                    frame_expected = 1 - frame_expected;
                }
                send_ack_frame();
            } 
            if (f.ack == frame_nr) {
                stop_timer(frame_nr);   //ack_expected 跟nextframetosend等同，因为窗口只有1，收到之后才会发下一帧所以可以等同 
                nbuffered--;
                frame_nr = 1 - frame_nr; 
            }
            break; 

        case DATA_TIMEOUT:
            dbg_event("---- DATA %d timeout\n", arg); 
            send_data_frame();
            break;
        }

        if (nbuffered < 1 && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
   }
}
//this is a test
