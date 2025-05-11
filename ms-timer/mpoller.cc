#include <assert.h>

#include "mpoller.h"
#include "include/mstimer.h"

namespace skynet_ext {
namespace ms_timer {

MPoller::MPoller() {
	poller = nullptr;
	nthreads_ = 0;
}

MPoller::~MPoller() {
	if (poller != nullptr) {
		delete []poller;
		poller = nullptr;
	}
	nthreads_ = 0;
}

int MPoller::Init(uint32_t nthreads) {
	if (nthreads == 0) {
		nthreads = 1;
	}
	nthreads_ = nthreads;
	poller = new Poller[nthreads];
	int ec = ErrCode::UNKNOWN_ERROR;
	uint32_t i;
	for (i = 0; i < nthreads; i++) {
		ec = poller[i].Init(i);
		if (ec != ErrCode::OK) {
			break;
		}
	}
	if (nthreads == i) {
		return ErrCode::OK;
	}
	delete []poller;
	poller = nullptr;
	return ec;
}

static MPoller *mpoller = nullptr;

int InitPoller(uint32_t nthreads) {
	assert(mpoller == nullptr);
	mpoller = new MPoller();
	int ec = mpoller->Init(nthreads);
	if (ec != ErrCode::OK) {
		delete mpoller;
		mpoller = nullptr;
	}
	return ec;
}

void ExitPoller() {
	if (mpoller != nullptr) {
		delete mpoller;
		mpoller = nullptr;
	}
}

int StartTimer(uint32_t service_handle, int session, int count, uint32_t interval_ms) {
	if (session == 0) {
		return ErrCode::API_PARAM2_ERROR;
	}
	if (interval_ms <= 0) {
		return ErrCode::API_PARAM4_ERROR;
	}
	RequestMsg request;
	request.u.add = {
		.service_handle = service_handle,
		.session = session,
		.count = count,
		.interval_ms = interval_ms
	};

	int index = service_handle % mpoller->nthreads_;
	mpoller->poller[index].SendRequest(&request, 'A', sizeof(request.u.add));
	return ErrCode::OK;
}

void StopTimer(uint32_t service_handle, int session) {
	RequestMsg request;
	request.u.del = {
		.service_handle = service_handle,
		.session = session
	};

	int index = service_handle % mpoller->nthreads_;
	mpoller->poller[index].SendRequest(&request, 'D', sizeof(request.u.del));
}

} // namespace ms_timer
} // namespace skynet_ext