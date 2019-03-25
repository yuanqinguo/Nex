#ifndef CNET_EVENT_H
#define CNET_EVENT_H

#include "event2/event.h"
#include "event2/listener.h"
#include "event2/bufferevent.h"
#include "event2/buffer.h"
#include "event2/util.h"
#include "event2/thread.h"

#include "Locker.h"

#include<deque>
#include <map>
#include <string>

using namespace std;

#define RECV_BUFF_SIZE 10240

struct NetClient
{
	struct bufferevent* bev;
	long active_time;
	void* user_data;
	int recv_index;
	char* recv_buffer;
};

struct AcceptArg
{
	void* obj;
	void* evbase;	
};

struct NetResponse
{
	struct bufferevent* bev;
	char* send_buffer;
};

class CNetEvent
{
public:
	CNetEvent();
	~CNetEvent();

	//启动网络服务
	int start(int port);

	void add_response(struct bufferevent* bev, char* msg_buffer);
private:
	//事件循环
	static void run(void* arg);

	//接受客户端连接
	static void accept_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *user_data);
	void worker_accept(evutil_socket_t fd, struct event_base* base);

	//定时器回调,检查链接超时
	static void timeout_cb(evutil_socket_t fd, short ev, void* arg);
	void worker_timeout();

	//定时器,可激活立即调用，用于回复队列进行数据回复
	static void response_cb(evutil_socket_t fd, short ev, void* arg);
	//回复函数
	void worker_response();

	//读数据回调
	static void read_cb(struct bufferevent* bev, void* arg);
	void worker_read(struct bufferevent* bev, NetClient* client);

	//异常回调
	static void event_cb(struct bufferevent* bev, short event, void* arg);
	void worker_event(struct bufferevent *bev, short event, NetClient* client);

	

	//释放资源
	void free_netclient(NetClient*& pClient)
	{
		if (pClient)
		{
			struct bufferevent* bev = pClient->bev;
			if (bev)
			{
				bufferevent_free(bev);
			}
			bev = NULL;

			if (pClient->recv_buffer)
			{
				delete pClient->recv_buffer;
				pClient->recv_buffer = NULL;
			}

			pClient->user_data = NULL;
			delete pClient;
			pClient = NULL;
		}
	}

	void free_netresponse(NetResponse*& resp)
	{
		if (resp->send_buffer)
		{
			delete resp->send_buffer;
			resp->send_buffer = NULL;
		}
		resp->bev = NULL;

		delete resp;
		resp = NULL;
	}
private:
	std::string m_strAddr;
	int m_iPort;
	bool m_isStart;
	
	//网络连接信息
	std::map<int, NetClient*> m_client_map;
	//消息回复队列
	std::deque<NetResponse*> m_response_deq;
	Locker m_resvct_lock;

	//定时器映射
	std::map<int, struct event*> m_timer_map;

	int m_index;
};

#endif //CNET_EVENT_H