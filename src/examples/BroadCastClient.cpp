#include <functional>
#include <time.h>
#include <stdio.h>
#include <thread>
#include <iostream>
#include <assert.h>
#include <chrono>
#include <memory>
#include <thread>
#include <atomic>

#include "packet.h"

#include "SocketLibFunction.h"

#include "EventLoop.h"
#include "DataSocket.h"
#include "timer.h"

using namespace std;

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        fprintf(stderr, "Usage: <server ip> <server port> <session num> <packet size>\n");

        exit(-1);
    }

    std::string ip = argv[1];
    int client_num = atoi(argv[2]);
    int packet_len = atoi(argv[3]);
    int port_num = atoi(argv[4]);

    ox_socket_init();

    EventLoop       mainLoop;

    /*  �ͻ���IO�߳�   */

    std::thread* ts = new std::thread([&mainLoop, client_num, packet_len, port_num, ip]{
        printf("start one client thread \n");
        /*  �ͻ���eventloop*/
        EventLoop clientEventLoop;

        char* senddata = (char*)malloc(packet_len);

        /*  ��Ϣ����С���� */
        atomic_llong  packet_num = ATOMIC_VAR_INIT(0);
        atomic_llong total_recv = ATOMIC_VAR_INIT(0);
        TimerMgr tm;
        for (int i = 0; i < client_num; i++)
        {
            int client = ox_socket_connect(ip.c_str(), port_num);
            ox_socket_nodelay(client);

            DataSocket::PTR pClient = new DataSocket(client, 1024 * 1024);
            pClient->setEnterCallback([&](DataSocket::PTR ds){

                LongPacket sp;
                sp.setOP(1);
                sp.writeINT64((int64_t)ds);
                sp.writeBinary(senddata, packet_len);
                sp.end();

                for (int i = 0; i < 1; ++i)
                {
                    ds->send(sp.getData(), sp.getLen());
                }

                /*  ���Է�����Ϣ���У�Ȼ���������̵߳�eventloop��Ȼ�����߳�ͨ����Ϣ����ȥ��ȡ*/
                ds->setDataCallback([&total_recv, &packet_num](DataSocket::PTR ds, const char* buffer, size_t len){
                    const char* parse_str = buffer;
                    int total_proc_len = 0;
                    int left_len = len;

                    while (true)
                    {
                        bool flag = false;
                        if (left_len >= sizeof(sizeof(uint16_t) + sizeof(uint16_t)))
                        {
                            ReadPacket rp(parse_str, left_len);
                            uint16_t packet_len = rp.readINT16();
                            if (left_len >= packet_len && packet_len >= (sizeof(uint16_t) + sizeof(uint16_t)))
                            {
                                total_recv += packet_len;
                                packet_num++;

                                ReadPacket rp(parse_str, packet_len);
                                rp.readINT16();
                                rp.readINT16();
                                int64_t addr = rp.readINT64();

                                if (addr == (int64_t)(ds))
                                {
                                    ds->send(parse_str, packet_len);
                                }

                                total_proc_len += packet_len;
                                parse_str += packet_len;
                                left_len -= packet_len;
                                flag = true;
                            }
                        }

                        if (!flag)
                        {
                            break;
                        }
                    }

                    return total_proc_len;
                });

                ds->setDisConnectCallback([](DataSocket::PTR arg){
                    delete arg;
                });
            });

            clientEventLoop.pushAsyncProc([&, pClient](){
                if (!pClient->onEnterEventLoop(&clientEventLoop))
                {
                    delete pClient;
                }
            });
        }

        int64_t now = ox_getnowtime();
        while (true)
        {
            clientEventLoop.loop(tm.NearEndMs());
            tm.Schedule();
            if ((ox_getnowtime() - now) >= 1000)
            {
                cout << "total recv:" << (total_recv / 1024) / 1024 << " M /s" << " , num " << packet_num << endl;
                now = ox_getnowtime();
                total_recv = 0;
                packet_num = 0;
            }
        }
    });

    ts->join();
}