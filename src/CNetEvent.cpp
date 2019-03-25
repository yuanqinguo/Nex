#include "CNetEvent.h"
#include "wLog.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>


CNetEvent::CNetEvent()
{
	m_strAddr = "0.0.0.0";
	m_index = 0;
	m_isStart = false;
}

CNetEvent::~CNetEvent()
{

}

int CNetEvent::start(int port)
{
	if (m_isStart == true)
		return 0;

	m_iPort = port;
	m_isStart = true;

	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(m_iPort);
	inet_aton(m_strAddr.c_str(), &sin.sin_addr);

	for(int i=0; i<4; i++)
    {
    	int cpid = fork();
        if (cpid < 0)
        {
            printf("fork failed!\n");
        }
        else if (cpid == 0)
        {
        	evthread_use_pthreads();
    		struct event_base* base = event_base_new();
			if (!base)
			{
				printf("Could not initialize libevent!");
				return -1;
			}

			struct AcceptArg* pArg = new AcceptArg;
			pArg->evbase = base;
			pArg->obj = this;

        	struct evconnlistener* listener = 
				evconnlistener_new_bind(base, accept_cb, (void *)pArg,
				LEV_OPT_REUSEABLE|LEV_OPT_THREADSAFE|LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE_PORT, 1024,
				(struct sockaddr*)&sin,
				sizeof(sin));

			//注册定时器
			struct timeval tv;
			tv.tv_sec = 1; //1s
			tv.tv_usec = 0; //0ms

			//注册回复请求，发送数据定时器，可在处理完请求之后进行激活回调
			struct event* timerEv = event_new(NULL, -1, 0, NULL, NULL);
			event_assign(timerEv, base, -1, EV_TIMEOUT | EV_PERSIST,
				timeout_cb, this);

			event_add(timerEv,&tv);

			//注册回复数据定时器，可主动激活定时器
			tv.tv_sec = 1; //10s
			tv.tv_usec = 0; //0ms

			//注册回复请求，发送数据定时器，可在处理完请求之后进行激活回调
			struct event* res_timerEv = event_new(NULL, -1, 0, NULL, NULL);
			event_assign(res_timerEv, base, -1, EV_TIMEOUT | EV_PERSIST,
				response_cb, this);
			m_timer_map.insert(std::make_pair(getpid(), res_timerEv));

			event_add(res_timerEv,&tv);

			event_base_dispatch(base);

			event_free(timerEv);
			event_free(res_timerEv);

			evconnlistener_free(listener);
		    event_base_free(base);

		}
	}

	return 0;
}

void CNetEvent::accept_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *arg)
{
	struct AcceptArg* pArg = (struct AcceptArg*)arg;
	CNetEvent* pThisObj = (CNetEvent*)pArg->obj;

	if (pThisObj)
	{
		pThisObj->worker_accept(fd, (struct event_base*)pArg->evbase);
	}
}
void CNetEvent::worker_accept(evutil_socket_t fd, struct event_base* base)
{
	//printf("CNetEvent::worker_accept fd:%d, pid : %d\n", fd, getpid());
	struct bufferevent* bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	if(NULL == bev)
	{
		WLogError("CNetEvent::worker_accept::bufferevent_socket_new error!");
	}
	else
	{
		NetClient* client = new NetClient;
		client->bev = bev;
		client->active_time = time(NULL);
		client->recv_index = 0;
		client->recv_buffer = NULL;
		client->user_data = this;
		m_client_map.insert(std::make_pair(fd, client));

		bufferevent_setcb(bev, read_cb, NULL, event_cb, (void*)client);
		bufferevent_enable(bev, EV_READ|EV_WRITE);

		//WLogDebug("CNetEvent::worker_accept::client:size: %d", m_client_vct.size());
	}
}

void CNetEvent::timeout_cb(evutil_socket_t fd, short ev, void* arg)
{
	CNetEvent* pThisObj = (CNetEvent*)arg;
	if (pThisObj)
	{
		pThisObj->worker_timeout();
	}
}

void CNetEvent::worker_timeout()
{
	//检查socket超时
	time_t curTime = time(NULL);
	std::map<int, NetClient*>::iterator it = m_client_map.begin();

	int isize = m_client_map.size();
	if (isize > m_index)
	{
		for(int i=0; i<m_index; i++)
		{
			it++;
			if (it == m_client_map.end())
			{
				m_index = 0;
				it = m_client_map.begin();
			}
		}
	}
	else
    {
        m_index = 0;
    }

	for (int i=0; it != m_client_map.end() && i < 100; i++)
	{
		NetClient* pClient = it->second;
		long active_time = pClient->active_time;

		if ((curTime - active_time) > 60)
		{
			evutil_socket_t fd = bufferevent_getfd(pClient->bev);
			WLogDebug("CNetEvent::worker_timeout->client time out, fd:%d, index:%d, pid: %d", fd, m_index, getpid());
			it = m_client_map.erase(it);
			free_netclient(pClient);
		}
		else
		{
			it++;
		}

		m_index++;
	}
	WLogDebug("CNetEvent::worker_timeout::client-size:%d", m_client_map.size());
}

void CNetEvent::response_cb(evutil_socket_t fd, short ev, void* arg)
{
	CNetEvent* pThisObj = (CNetEvent*)arg;
	if (pThisObj)
	{
		pThisObj->worker_response();
	}
}

void CNetEvent::worker_response()
{
	WLogDebug("CNetEvent::worker_response::response-size:%d", m_response_deq.size());
	std::deque<NetResponse*>::iterator iter = m_response_deq.begin();
	if(iter == m_response_deq.end())
		return ;
	else
	{
		for( ; iter!=m_response_deq.end(); )
		{
			NetResponse* resp = *iter;
			{
				LockerGuard guard(m_resvct_lock);
				iter = m_response_deq.erase(iter);
			}

			evutil_socket_t fd = bufferevent_getfd(resp->bev);
			//数据回复
			int iret = bufferevent_write(resp->bev, resp->send_buffer, strlen(resp->send_buffer));
			if (iret != 0)
			{
				WLogError("CNetEvent::net_response::bufferevent_write error fd:%d, pid:%d\n", fd, getpid());
			}

			free_netresponse(resp);
		}
	}
}

void CNetEvent::read_cb(struct bufferevent* bev, void* arg)
{
	NetClient* client = (NetClient*)arg;
	if (client and client->user_data)
	{
		CNetEvent* pthis = (CNetEvent*)client->user_data;
		pthis->worker_read(bev, client);
	}
}

void CNetEvent::worker_read(struct bufferevent* bev, NetClient* client)
{
	client->active_time = time(NULL);

	if (client->recv_buffer == NULL)
	{
		client->recv_buffer = new char[RECV_BUFF_SIZE];
		memset(client->recv_buffer, 0, RECV_BUFF_SIZE);
		client->recv_index = 0;
	}

	while(1)
	{
		int len = bufferevent_read(bev, client->recv_buffer + client->recv_index, RECV_BUFF_SIZE-client->recv_index);

		client->recv_index += len;

		if (client->recv_index >= RECV_BUFF_SIZE)
		{	//缓冲区已满
			add_response(bev, client->recv_buffer);
			client->recv_index = 0;
			memset(client->recv_buffer, 0, RECV_BUFF_SIZE);
			continue;
		}

		if (len <= 0)
			//数据已读取完成
			break;
	}

	if (client->recv_index > 0)
	{
		add_response(bev, client->recv_buffer);
		client->recv_index = 0;
		memset(client->recv_buffer, 0, RECV_BUFF_SIZE);
	}

	std::map<int, struct event*>::iterator it = m_timer_map.find(getpid());
	if (it != m_timer_map.end())
	{
		event_active(it->second, EV_TIMEOUT, 0);
	}
}

void CNetEvent::add_response(struct bufferevent* bev, char* msg_buffer)
{
	NetResponse* resp = new NetResponse;
	resp->bev = bev;
	resp->send_buffer = new char[RECV_BUFF_SIZE];
	strcpy(resp->send_buffer, msg_buffer);

	{
		LockerGuard guard(m_resvct_lock);
		m_response_deq.push_back(resp);
	}
}

void CNetEvent::event_cb(struct bufferevent* bev, short event, void* arg)
{
	NetClient* client = (NetClient*)arg;
	if (client and client->user_data)
	{
		CNetEvent* pthis = (CNetEvent*)client->user_data;
		pthis->worker_event(bev, event, client);
	}
}

void CNetEvent::worker_event(struct bufferevent *bev, short event, NetClient* client)
{
	evutil_socket_t fd = bufferevent_getfd(bev);
	if ( (event && BEV_EVENT_EOF)
		|| (event && BEV_EVENT_ERROR) )
	{	//socket异常，直接关闭
		std::map<int, NetClient*>::iterator it = m_client_map.find(fd);
		if (it != m_client_map.end())
		{
			NetClient* client = it->second;
			free_netclient(client);

			m_client_map.erase(it);
		}
	}
}
