#include "tp_http/AsyncTimer.h"

#include "tp_utils/RefCount.h"
#include "tp_utils/MutexUtils.h"

#include <boost/asio/deadline_timer.hpp>

namespace tp_http
{

namespace
{
struct Alive_lt
{
  TPMutex mutex{TPM};
  bool alive{true};
};
}

//##################################################################################################
struct AsyncTimer::Private
{
  TP_REF_COUNT_OBJECTS("AsyncTimer::Private");

  std::function<void()> callback;
  int64_t timeoutMS;
  boost::asio::io_context* ioContext;

  std::shared_ptr<Alive_lt> alive{std::make_shared<Alive_lt>()};

  boost::asio::deadline_timer timer{*ioContext};

  //################################################################################################
  Private(const std::function<void()>& callback_, int64_t timeoutMS_, boost::asio::io_context* ioContext_):
    callback(callback_),
    timeoutMS(timeoutMS_),
    ioContext(ioContext_)
  {

  }

  //################################################################################################
  std::function<void(const boost::system::error_code& ec)> called = [this, weak=std::weak_ptr<Alive_lt>(alive)](const boost::system::error_code& ec)
  {
    if(ec.failed())
      return;

    auto a = weak.lock();
    if(!a)
      return;

    TP_MUTEX_LOCKER(a->mutex);

    if(!a->alive)
      return;

    callback();

    timer.expires_from_now(boost::posix_time::milliseconds(timeoutMS));
    timer.async_wait(called);
  };
};

//##################################################################################################
AsyncTimer::AsyncTimer(const std::function<void()>& callback, int64_t timeoutMS, boost::asio::io_context* ioContext):
  d(new Private(callback, timeoutMS, ioContext))
{
  d->timer.expires_from_now(boost::posix_time::milliseconds(d->timeoutMS));
  d->timer.async_wait(d->called);
}

//##################################################################################################
AsyncTimer::~AsyncTimer()
{
  d->timer.cancel();

  {
    TP_MUTEX_LOCKER(d->alive->mutex);
    d->alive->alive=false;
  }

  d->alive.reset();

  delete d;
}

}
