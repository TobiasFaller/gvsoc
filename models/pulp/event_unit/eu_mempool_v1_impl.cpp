/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <string.h>
#include <archi/eu/eu_mempool_v1.h>

class Core_event_unit;
class Event_unit;
class Dispatch_unit;
class Mutex_unit;


// Timing constants

// Cycles required by the core to really wakeup after his clock is back
// Could actually be split into 2 latencies:
//   - The one required by the event unit to grant the access.
//   - The one required by the core to continue after the grant is back.
//     This one should be 2 or 3 cycles and should be moved to the core.
#define EU_WAKEUP_REQ_LATENCY 6

// Cycles needed by the event unit to send back the clock once
// a core is waken-up
#define EU_WAKEUP_LATENCY 2



class Soc_event_unit {
public:

  Soc_event_unit(Event_unit *top);
  void reset();

  int nb_fifo_events;
  int nb_free_events;
  int fifo_event_head;
  int fifo_event_tail;
  int *fifo_event;
  int fifo_soc_event;

  vp::wire_slave<int>     soc_event_itf;

  vp::io_req_status_e ioReq(uint32_t offset, bool is_write, uint32_t *data);

  void check_state();

private:
  static void sync(void *__this, int event);

  Event_unit *top;
  vp::trace     trace;

};



class Mutex {
public:
  void reset();

  bool locked;
  std::list<int> waiting_list;
  uint32_t value;
  vp::io_req *waiting_reqs[1024];
};

class Mutex_unit {
public:
  Mutex_unit(Event_unit *top);

  void reset();

  Mutex *mutexes;
  vp::io_req_status_e req(vp::io_req *req, uint64_t offset, bool is_write, uint32_t *data, int core);

private:
  vp::io_req_status_e enqueue_sleep(Mutex *mutex, vp::io_req *req, int core_id) ;
  Event_unit *top;
  vp::trace     trace;
  int nb_mutexes;
  int mutex_event;
};



// class Dispatch {
// public:
//   uint32_t value;
//   uint32_t status_mask;     // Cores that must get the value before it can be written again
//   uint32_t config_mask;     // Cores that will get a valid value
//   uint32_t waiting_mask;
//   vp::io_req *waiting_reqs[1024];
// };

// class Dispatch_core
// {
// public:
//   int tail;
// };

// class Dispatch_unit {
// public:
//   Dispatch_unit(Event_unit *top);

//   vp::io_req_status_e req(vp::io_req *req, uint64_t offset, bool is_write, uint32_t *data, int core);
//   void reset();

//   vp::io_req_status_e enqueue_sleep(Dispatch *dispatch, vp::io_req *req, int core_id, bool is_caller=true);

//   unsigned int config;
//   int dispatch_event;
// private:
//   Event_unit *top;
//   Dispatch_core *core;
//   Dispatch *dispatches;
//   int size;
//   int fifo_head;
// };



class Barrier {
public:
  std::array<uint32_t, EU_MASK_REG_SIZE/4> core_mask;
  std::array<uint32_t, EU_MASK_REG_SIZE/4> status;
  std::array<uint32_t, EU_MASK_REG_SIZE/4> target_mask;
};

class Barrier_unit {
public:
  Barrier_unit(Event_unit *top);

  vp::io_req_status_e req(vp::io_req *req, uint64_t offset, bool is_write, uint32_t *data, int core);
  void reset();

private:
  void check_barrier(int barrier_id);

  Event_unit *top;
  vp::trace  trace;
  Barrier    *barriers;
  int        nb_barriers;
  int        barrier_event;
};



class Event_unit : public vp::component
{

  friend class Core_event_unit;
  // friend class Dispatch_unit;
  friend class Barrier_unit;
  friend class Mutex_unit;
  friend class Soc_event_unit;

public:

  Event_unit(const char *config);

  int build();
  void start();
  void reset(bool active);

  static vp::io_req_status_e req(void *__this, vp::io_req *req);
  static vp::io_req_status_e demux_req(void *__this, vp::io_req *req, int core);
  static void irq_ack_sync(void *__this, int irq, int core);

protected:

  vp::trace     trace;

  vp::io_slave in;

  Mutex_unit *mutex;
  Core_event_unit *core_eu;
  // Dispatch_unit *dispatch;
  Barrier_unit *barrier_unit;
  Soc_event_unit *soc_event_unit;

  int nb_core;


  vp::io_req_status_e sw_events_req(vp::io_req *req, uint64_t offset, bool is_write, uint32_t *data, int core = -1);
  void trigger_event(int event_mask, std::array<uint32_t, EU_MASK_REG_SIZE/4> core_mask);
  void send_event(int core, uint32_t mask);
  static void in_event_sync(void *__this, bool active, int id);

};



typedef enum
{
  CORE_STATE_NONE,
  CORE_STATE_WAITING_EVENT,
  CORE_STATE_WAITING_BARRIER
} Event_unit_core_state_e;

class Core_event_unit
{
public:
  static vp::io_req_status_e req(void *__this, vp::io_req *req);
  void build(Event_unit *top, int core_id);
  void set_status(uint32_t new_value);
  void clear_status(uint32_t mask);
  void reset();
  void check_state();
  vp::io_req_status_e req(vp::io_req *req, uint64_t offset, bool is_write, uint32_t *data);
  void check_wait_mask();
  void check_pending_req();
  void cancel_pending_req();
  vp::io_req_status_e wait_event(vp::io_req *req, Event_unit_core_state_e wait_state=CORE_STATE_WAITING_EVENT);
  vp::io_req_status_e put_to_sleep(vp::io_req *req, Event_unit_core_state_e wait_state=CORE_STATE_WAITING_EVENT);
  Event_unit_core_state_e get_state() { return state; }
  void irq_ack_sync(int irq, int core);
  static void wakeup_handler(void *__this, vp::clock_event *event);
  static void irq_wakeup_handler(void *__this, vp::clock_event *event);

  vp::io_slave demux_in;

  uint32_t status;
  uint32_t evt_mask;
  uint32_t irq_mask;
  uint32_t clear_evt_mask;

  int sync_irq;

private:
  Event_unit *top;
  int core_id;
  Event_unit_core_state_e state;
  vp::io_req *pending_req;

  vp::wire_master<bool> barrier_itf;
  vp::wire_slave<bool> in_event_itf[1024];

  vp::wire_master<bool>    clock_itf;

  vp::wire_master<int>    irq_req_itf;
  vp::wire_slave<int>     irq_ack_itf;

  vp::clock_event *wakeup_event;
  vp::clock_event *irq_wakeup_event;

  vp::reg_1  is_active;

};



Event_unit::Event_unit(const char *config)
: vp::component(config)
{
  nb_core = get_config_int("nb_core");
}

void Event_unit::reset(bool active)
{
  if (active)
  {
    for (int i=0; i<nb_core; i++)
    {
      core_eu[i].reset();
    }
    // dispatch->reset();
    barrier_unit->reset();
    mutex->reset();
    soc_event_unit->reset();
  }
}

vp::io_req_status_e Event_unit::req(void *__this, vp::io_req *req)
{
  Event_unit *_this = (Event_unit *)__this;

  uint64_t offset = req->get_addr();
  uint8_t *data = req->get_data();
  uint64_t size = req->get_size();
  bool is_write = req->get_is_write();

  _this->trace.msg("Event_unit access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, is_write);

  if (size != 4)
  {
    _this->trace.warning("Only 32 bits accesses are allowed\n");
    return vp::IO_REQ_INVALID;
  }

  if (offset >= EU_CORES_AREA_OFFSET && offset < EU_CORES_AREA_OFFSET + EU_CORES_AREA_SIZE)
  {
    unsigned int core_id = EU_CORE_AREA_COREID_GET(offset - EU_CORES_AREA_OFFSET);
    if (core_id >= _this->nb_core) return vp::IO_REQ_INVALID;
    return _this->core_eu[core_id].req(req, offset - EU_CORE_AREA_OFFSET_GET(core_id), is_write, (uint32_t *)data);
  }
  else if (offset >= EU_SOC_EVENTS_AREA_OFFSET && offset < EU_SOC_EVENTS_AREA_OFFSET + EU_SOC_EVENTS_AREA_SIZE)
  {
    return _this->soc_event_unit->ioReq(offset - EU_SOC_EVENTS_AREA_OFFSET, is_write, (uint32_t *)data);
  }
  else if (offset >= EU_SW_EVENTS_AREA_BASE && offset < EU_SW_EVENTS_AREA_BASE + EU_SW_EVENTS_AREA_SIZE)
  {
    return _this->sw_events_req(req, offset - EU_SW_EVENTS_AREA_BASE, is_write, (uint32_t *)data);
  }
  else if (offset >= EU_BARRIER_AREA_OFFSET && offset < EU_BARRIER_AREA_OFFSET + EU_BARRIER_AREA_SIZE)
  {
    return _this->barrier_unit->req(req, offset - EU_BARRIER_AREA_OFFSET, is_write, (uint32_t *)data, -1);
  }

  return vp::IO_REQ_INVALID;

}

/**
 * Software event request
 * @param  req      Request
 * @param  offset   Address offset
 * @param  is_write True is request is write
 * @param  data     Pointer to data to write or return read data
 * @param  core     Core who issued the request through the demux interface. -1 if the request came through the slave port.
 * @return          Status
 */
vp::io_req_status_e Event_unit::sw_events_req(vp::io_req *req, uint64_t offset, bool is_write, uint32_t *data, int core /*= -1*/)
{
  if (offset >= EU_CORE_TRIGG_SW_EVENT && offset < (EU_CORE_TRIGG_SW_EVENT + EU_CORE_TRIGG_SW_EVENT_SIZE))
  {
    if (!is_write) return vp::IO_REQ_INVALID;

    trace.msg("SW event trigger (eventMask: 0x%x)\n", *data);
    // Trigger all software events in the mask for all cores
    for (int i = 0; i < nb_core; i++)
    {
      send_event(i, *data);
    }
  }
  else if (offset >= EU_CORE_TRIGG_SW_EVENT_WAIT && offset <  EU_CORE_TRIGG_SW_EVENT_WAIT_SIZE)
  {
    // Register can only be read and only through the demux interface
    if (is_write || core == -1 || core >= nb_core)
    {
      trace.warning("EU_CORE_TRIGG_SW_EVENT_WAIT can only be read through the demux interface (is_write = %d, core = %d)\n", is_write, core);
      return vp::IO_REQ_INVALID;
    }

    int event = EU_CORE_TRIGG_SW_EVENT_WAIT_EVENT_GET(offset - EU_CORE_TRIGG_SW_EVENT_WAIT);
    Core_event_unit* eu = &core_eu[core];
    trace.msg("Event trigger and wait (event: %d, coreMask: 0x%x)\n", event, *data);
    for (int i = 0; i < nb_core; i++)
    {
      send_event(i, 1 << event);
    }
    vp::io_req_status_e err = eu->wait_event(req);
    *data = eu->evt_mask & eu->status;
    return err;
  }
  else if (offset >= EU_CORE_TRIGG_SW_EVENT_WAIT_CLEAR && offset <  EU_CORE_TRIGG_SW_EVENT_WAIT_CLEAR_SIZE)
  {
    // Register can only be read and only through the demux interface
    if (is_write || core == -1 || core >= nb_core)
    {
      trace.warning("EU_CORE_TRIGG_SW_EVENT_WAIT can only be read through the demux interface (is_write = %d, core = %d)\n", is_write, core);
      return vp::IO_REQ_INVALID;
    }

    int event = EU_CORE_TRIGG_SW_EVENT_WAIT_EVENT_GET(offset - EU_CORE_TRIGG_SW_EVENT_WAIT);
    Core_event_unit* eu = &core_eu[core];
    trace.msg("Event trigger and wait (event: %d, coreMask: 0x%x)\n", event, *data);
    for (int i = 0; i < nb_core; i++)
    {
      send_event(i, 1 << event);
    }
    eu->clear_evt_mask = eu->evt_mask;
    vp::io_req_status_e err = eu->wait_event(req);
    *data = eu->evt_mask & eu->status;
    return err;
  }
  else
  {
    trace.warning("UNIMPLEMENTED offset at %s %d\n", __FILE__, __LINE__);
    return vp::IO_REQ_INVALID;
  }

  return vp::IO_REQ_OK;
}

void Event_unit::trigger_event(int event_mask, std::array<uint32_t, EU_MASK_REG_SIZE/4> core_mask)
{
  // std::assert(nb_core <= core_mask.size()*32, "Register size too small for number of cores");
  for (int i = 0; i < nb_core; ++i)
  {
    if (core_mask[i/32] & (1 << (i % 32)))
    {
      send_event(i, event_mask);
    }
  }
}

void Event_unit::send_event(int core, uint32_t mask)
{
  trace.msg("Triggering event (core: %d, mask: 0x%x)\n", core, mask);
  Core_event_unit *eu = &core_eu[core];
  eu->set_status(eu->status | mask);
  eu->check_state();
}

void Core_event_unit::build(Event_unit *top, int core_id)
{
  this->top = top;
  this->core_id = core_id;

  this->top->new_reg("core_" + std::to_string(core_id) + "/active", &this->is_active, 1);

  demux_in.set_req_meth_muxed(&Event_unit::demux_req, core_id);
  top->new_slave_port("demux_in_" + std::to_string(core_id), &demux_in);

  wakeup_event = top->event_new((void *)this, Core_event_unit::wakeup_handler);
  irq_wakeup_event = top->event_new((void *)this, Core_event_unit::irq_wakeup_handler);

  top->new_master_port("irq_req_" + std::to_string(core_id), &irq_req_itf);

  top->new_master_port("clock_" + std::to_string(core_id), &clock_itf);

  irq_ack_itf.set_sync_meth_muxed(&Event_unit::irq_ack_sync, core_id);
  top->new_slave_port("irq_ack_" + std::to_string(core_id), &irq_ack_itf);

  for (int i=0; i<top->nb_core; i++)
  {
    in_event_itf[i].set_sync_meth_muxed(&Event_unit::in_event_sync, (core_id << 16 | i));
    top->new_slave_port("in_event_" + std::to_string(i) + "_pe_" + std::to_string(core_id), &in_event_itf[i]);
  }
}

vp::io_req_status_e Core_event_unit::req(vp::io_req *req, uint64_t offset, bool is_write, uint32_t *data)
{
  if (offset == EU_CORE_MASK)
  {
    if (!is_write) *data = evt_mask;
    else {
      evt_mask = *data;
      top->trace.msg("Updating event mask (newValue: 0x%x)\n", *data);
      check_state();
    }
  }
  else if (offset == EU_CORE_MASK_AND)
  {
    if (!is_write) return vp::IO_REQ_INVALID;
    evt_mask &= ~*data;
    top->trace.msg("Clearing event mask (mask: 0x%x, newValue: 0x%x)\n", *data, evt_mask);
    check_state();
  }
  else if (offset == EU_CORE_MASK_OR)
  {
    if (!is_write) return vp::IO_REQ_INVALID;
    evt_mask |= *data;
    top->trace.msg("Setting event mask (mask: 0x%x, newValue: 0x%x)\n", *data, evt_mask);
    check_state();
  }
  else if (offset == EU_CORE_MASK_IRQ)
  {
    if (!is_write) *data = irq_mask;
    else {
      top->trace.msg("Updating irq mask (newValue: 0x%x)\n", *data);
      irq_mask = *data;
      check_state();
    }
  }
  else if (offset == EU_CORE_MASK_IRQ_AND)
  {
    if (!is_write) return vp::IO_REQ_INVALID;
    irq_mask &= ~*data;
    top->trace.msg("Clearing irq mask (mask: 0x%x, newValue: 0x%x)\n", *data, irq_mask);
    check_state();
  }
  else if (offset == EU_CORE_MASK_IRQ_OR)
  {
    if (!is_write) return vp::IO_REQ_INVALID;
    irq_mask |= *data;
    top->trace.msg("Setting irq mask (mask: 0x%x, newValue: 0x%x)\n", *data, irq_mask);
    check_state();
  }
  else if (offset == EU_CORE_STATUS)
  {
    if (is_write) return vp::IO_REQ_INVALID;
    *data = this->is_active.get();
    return vp::IO_REQ_OK;
  }
  else if (offset == EU_CORE_BUFFER)
  {
    if (is_write) return vp::IO_REQ_INVALID;
    *data = status;
    return vp::IO_REQ_OK;
  }
  else if (offset == EU_CORE_BUFFER_MASKED)
  {
    if (is_write) return vp::IO_REQ_INVALID;
    *data = status & evt_mask;
    return vp::IO_REQ_OK;
  }
  else if (offset == EU_CORE_BUFFER_IRQ_MASKED)
  {
    if (is_write) return vp::IO_REQ_INVALID;
    *data = status & irq_mask;
  }
  else if (offset == EU_CORE_BUFFER_CLEAR)
  {
    if (!is_write) return vp::IO_REQ_INVALID;
    clear_status(*data);
    top->trace.msg("Clearing buffer status (mask: 0x%x, newValue: 0x%x)\n", *data, status);
    check_state();
    return vp::IO_REQ_OK;
  }
  else if (offset == EU_CORE_EVENT_WAIT)
  {
    if (is_write) return vp::IO_REQ_INVALID;
    top->trace.msg("Wait\n");
    vp::io_req_status_e err = wait_event(req);
    *data = evt_mask & status;
    return err;
  }
  else if (offset == EU_CORE_EVENT_WAIT_CLEAR)
  {
    top->trace.msg("Wait and clear\n");
    clear_evt_mask = evt_mask;
    vp::io_req_status_e err = wait_event(req);
    *data = evt_mask & status;
    return err;
  }
  else
  {
    return vp::IO_REQ_INVALID;
  }

  return vp::IO_REQ_OK;
}

void Event_unit::irq_ack_sync(void *__this, int irq, int core)
{
  Event_unit *_this = (Event_unit *)__this;

  _this->trace.msg("Received IRQ acknowledgement (core: %d, irq: %d)\n", core, irq);

  _this->core_eu[core].irq_ack_sync(irq, core);
}

vp::io_req_status_e Event_unit::demux_req(void *__this, vp::io_req *req, int core)
{
  Event_unit *_this = (Event_unit *)__this;

  uint64_t offset = req->get_addr();
  uint8_t *data   = req->get_data();
  uint64_t size   = req->get_size();
  bool is_write   = req->get_is_write();

  _this->trace.msg("Demux event_unit access (core: %d, offset: 0x%x, size: 0x%x, is_write: %d)\n", core, offset, size, is_write);

  if (size != 4)
  {
    _this->trace.warning("Only 32 bits accesses are allowed\n");
    return vp::IO_REQ_INVALID;
  }

  if (offset >= EU_CORE_DEMUX_OFFSET && offset < EU_CORE_DEMUX_OFFSET + EU_CORE_DEMUX_SIZE)
  {
    return _this->core_eu[core].req(req, offset - EU_CORE_DEMUX_OFFSET, is_write, (uint32_t *)data);
  }
  else if (offset >= EU_MUTEX_DEMUX_OFFSET && offset < EU_MUTEX_DEMUX_OFFSET + EU_MUTEX_DEMUX_SIZE)
  {
    return _this->mutex->req(req, offset - EU_MUTEX_DEMUX_OFFSET, is_write, (uint32_t *)data, core);
  }
  // else if (offset >= EU_DISPATCH_DEMUX_OFFSET && offset < EU_DISPATCH_DEMUX_OFFSET + EU_DISPATCH_DEMUX_SIZE)
  // {
  //   return _this->dispatch->req(req, offset - EU_DISPATCH_DEMUX_OFFSET, is_write, (uint32_t *)data, core);
  // }
  else if (offset >= EU_SW_EVENTS_DEMUX_OFFSET && offset < EU_SW_EVENTS_DEMUX_OFFSET + EU_SW_EVENTS_DEMUX_SIZE)
  {
    return _this->sw_events_req(req, offset - EU_SW_EVENTS_DEMUX_OFFSET, is_write, (uint32_t *)data, core);
  }
  else if (offset >= EU_BARRIER_DEMUX_OFFSET && offset < EU_BARRIER_DEMUX_OFFSET + EU_BARRIER_DEMUX_SIZE)
  {
    return _this->barrier_unit->req(req, offset - EU_BARRIER_DEMUX_OFFSET, is_write, (uint32_t *)data, core);
  }

  return vp::IO_REQ_INVALID;
}

void Event_unit::in_event_sync(void *__this, bool active, int id)
{
  Event_unit *_this = (Event_unit *)__this;
  int core_id = id >> 16;
  int event_id = id & 0xffff;
  _this->trace.msg("Received input event (core: %d, event: %d, active: %d)\n", core_id, event_id, active);
  Core_event_unit *eu = &_this->core_eu[core_id];
  eu->set_status(eu->status | (1<<event_id));
  eu->check_state();
}

int Event_unit::build()
{
  traces.new_trace("trace", &trace, vp::DEBUG);

  in.set_req_meth(&Event_unit::req);
  new_slave_port("input", &in);

  core_eu = (Core_event_unit *)new Core_event_unit[nb_core];
  mutex = new Mutex_unit(this);
  // dispatch = new Dispatch_unit(this);
  barrier_unit = new Barrier_unit(this);
  soc_event_unit = new Soc_event_unit(this);

  for (int i=0; i<nb_core; i++)
  {
    core_eu[i].build(this, i);
  }

  return 0;
}

void Event_unit::start()
{
}

extern "C" void *vp_constructor(const char *config)
{
  return (void *)new Event_unit(config);
}



void Core_event_unit::irq_ack_sync(int irq, int core)
{
  clear_status(1<<irq);
  sync_irq = -1;

  check_state();
}



void Core_event_unit::set_status(uint32_t new_value)
{
  status = new_value;
}

void Core_event_unit::clear_status(uint32_t mask)
{
  status = status & ~mask;
  top->soc_event_unit->check_state();
}


void Core_event_unit::check_pending_req()
{
  pending_req->get_resp_port()->resp(pending_req);
}

void Core_event_unit::cancel_pending_req()
{
  pending_req = NULL;
}



void Core_event_unit::check_wait_mask()
{
  if (clear_evt_mask)
  {
    clear_status(clear_evt_mask);
    top->trace.msg("Clear event after wake-up (evtMask: 0x%x, status: 0x%x)\n", clear_evt_mask, status);
    clear_evt_mask = 0;
  }
}

vp::io_req_status_e Core_event_unit::put_to_sleep(vp::io_req *req, Event_unit_core_state_e wait_state)
{
  state = wait_state;
  this->is_active.set(0);
  this->clock_itf.sync(0);
  pending_req = req;
  return vp::IO_REQ_PENDING;
}

vp::io_req_status_e Core_event_unit::wait_event(vp::io_req *req, Event_unit_core_state_e wait_state)
{
  top->trace.msg("Wait request (status: 0x%x, evt_mask: 0x%x)\n", status, evt_mask);

  // This takes 2 cycles for the event unit to clock-gate the core with replying
  // so this is seen as a latency of 2 cycles from the core point of view.
  // This will be added by the core after it is waken up.
  // Also if the event is already there it takes 2 cycles just to decide that we don't
  // go to sleep.
  req->inc_latency(EU_WAKEUP_REQ_LATENCY);

  // Experimental model where the core always go to sleep even if the event is there
  // This gives much better tming results on barrier.
  // Check if it is also the case for other features and check in the RTL how it is
  // handled.
  if (evt_mask & status)
  {
    // Case where the core ask for clock-gating but the event status prevent him from doing so
    // In this case, don't forget to clear the status in case of wait and clear.
    // Still apply the latency as to core will go to sleep before continuing.
    top->trace.msg("Activating clock (core: %d)\n", core_id);
    check_wait_mask();
    pending_req = req;
    top->event_enqueue(wakeup_event, EU_WAKEUP_LATENCY);
    return vp::IO_REQ_PENDING;
  }
  else
  {
    return this->put_to_sleep(req, wait_state);
  }
}

void Core_event_unit::reset()
{
  status = 0;
  evt_mask = 0;
  irq_mask = 0;
  clear_evt_mask = 0;
  sync_irq = -1;
  state = CORE_STATE_NONE;
  this->clock_itf.sync(1);
}

void Core_event_unit::wakeup_handler(void *__this, vp::clock_event *event)
{
  Core_event_unit *_this = (Core_event_unit *)__this;
  _this->top->trace.msg("Replying to core after wakeup (core: %d)\n", _this->core_id);
  _this->is_active.set(1);
  _this->clock_itf.sync(1);
  _this->check_pending_req();
  _this->check_state();
}

void Core_event_unit::irq_wakeup_handler(void *__this, vp::clock_event *event)
{
  Core_event_unit *_this = (Core_event_unit *)__this;
  _this->top->trace.msg("IRQ wakeup\n");
  _this->is_active.set(1);
  _this->clock_itf.sync(1);
  _this->check_state();
}

void Core_event_unit::check_state()
{
  //top->trace.msg("Checking core state (coreId: %d, active: %d, waitingEvent: %d, status: 0x%llx, evtMask: 0x%llx, irqMask: 0x%llx)\n", coreId, active, waitingEvent, status, evtMask, irqMask);

  uint32_t status_irq_masked = status & irq_mask;
  uint32_t status_evt_masked = status & evt_mask;
  int irq = status_irq_masked ? 31 - __builtin_clz(status_irq_masked) : -1;

  top->trace.msg("Checking core state (coreId: %d, active: %d, status: 0x%llx, evtMask: 0x%llx, irqMask: 0x%llx)\n", core_id, this->is_active.get(), status, evt_mask, irq_mask);

  if (this->is_active.get())
  {
    if (irq != sync_irq) {
      top->trace.msg("Updating irq req (core: %d, irq: %d)\n", core_id, irq);
      sync_irq = irq;
      irq_req_itf.sync(irq);
    }
  }

  if (!this->is_active.get())
  {
    if (status_irq_masked && !status_evt_masked)
    {
      // There is an active IRQ but no event, the core must be be waken up
      // just for the duration of the IRQ handler. The elw instruction will
      // replay the access, so we must keep the state as it is to resume
      // the on-going synchronization.
      top->trace.msg("Activating clock for IRQ handling(core: %d)\n", core_id);

      if (!irq_wakeup_event->is_enqueued())
      {
        top->event_enqueue(irq_wakeup_event, EU_WAKEUP_LATENCY);
        sync_irq = -1;
      }
    }
    else
    {
      switch (state)
      {
        case CORE_STATE_WAITING_EVENT:
        case CORE_STATE_WAITING_BARRIER:
        if (status_evt_masked)
        {
          top->trace.msg("Activating clock (core: %d)\n", core_id);
          state = CORE_STATE_NONE;
          check_wait_mask();
          if (!wakeup_event->is_enqueued())
          {
            top->event_enqueue(wakeup_event, EU_WAKEUP_LATENCY);
          }
        }
        break;
      }
    }
  }
}



/****************
 * MUTEX UNIT
 ****************/
Mutex_unit::Mutex_unit(Event_unit *top)
: top(top)
{
  top->traces.new_trace("mutex/trace", &trace, vp::DEBUG);
  nb_mutexes = top->get_config_int("**/properties/mutex/nb_mutexes");
  mutex_event = top->get_config_int("**/properties/events/mutex");
  mutexes = new Mutex[nb_mutexes];
}


vp::io_req_status_e Mutex_unit::enqueue_sleep(Mutex *mutex, vp::io_req *req, int core_id) {
  Core_event_unit *core_eu = &top->core_eu[core_id];

  // Enqueue the request so that the core can be unstalled when a value is pushed
  mutex->waiting_reqs[core_id] = req;
  mutex->waiting_list.push_back(core_id);

  // Don't forget to remember to clear the event after wake-up by the dispatch event
  core_eu->clear_evt_mask = 1<<mutex_event;

  return core_eu->wait_event(req);
}


void Mutex_unit::reset()
{
  for (int i=0; i<nb_mutexes; i++)
  {
    mutexes[i].reset();
  }
}

void Mutex::reset()
{
  locked = false;
  waiting_list.clear();
}

vp::io_req_status_e Mutex_unit::req(vp::io_req *req, uint64_t offset, bool is_write, uint32_t *data, int core)
{
  unsigned int id = EU_MUTEX_AREA_MUTEXID_GET(offset);
  if (id >= nb_mutexes) return vp::IO_REQ_INVALID;


  Mutex *mutex = &mutexes[id];
  Core_event_unit *evtUnit = &top->core_eu[core];
  top->trace.msg("Received mutex IO access (offset: 0x%x, mutex: %d, is_write: %d)\n", offset, id, is_write);

  if (!is_write)
  {
    if (!mutex->locked)
    {
      // The mutex is free, just lock it
      top->trace.msg("Locking mutex (mutex: %d, coreId: %d)\n", id, core);
      mutex->locked = 1;
    }
    else
    {
      // The mutex is locked, put the core to sleep
      top->trace.msg("Mutex already locked, waiting (mutex: %d, coreId: %d)\n", id, core);
      return enqueue_sleep(mutex, req, core);
    }
  }
  else
  {
    mutex->value = *(uint32_t *)req->get_data();

    // The core is unlocking the mutex, check if we have to wake-up someone
    if (!mutex->waiting_list.empty())
    {
      int i = mutex->waiting_list.front();
      mutex->waiting_list.pop_front();
      top->trace.msg("Transfering mutex lock (mutex: %d, fromCore: %d, toCore: %d)\n", id, core, i);
      // Clear the mask and wake-up the elected core. Don't unlock the mutex, as it is
      // taken by the new core
      top->trace.msg("Waking-up core waiting for dispatch value (coreId: %d)\n", i);
      vp::io_req *waiting_req = mutex->waiting_reqs[i];

      // Store the mutex value into the pending request
      // Don't reply now to the initiator, this will be done by the wakeup event
      // to introduce some delays
      *(uint32_t *)waiting_req->get_data() = mutex->value;

      // And trigger the event to the core
      top->send_event(i, 1<<mutex_event);
    }
    else
    {
      // No one waiting, just unlock the mutex
      top->trace.msg("Unlocking mutex (mutex: %d, coreId: %d)\n", id, core);
      mutex->locked = 0;
    }
  }

  return vp::IO_REQ_OK;
}



/****************
 * DISPATCH UNIT
 ****************/

// vp::io_req_status_e Dispatch_unit::enqueue_sleep(Dispatch *dispatch, vp::io_req *req, int core_id, bool is_caller) {
//   Core_event_unit *core_eu = &top->core_eu[core_id];

//   // Enqueue the request so that the core can be unstalled when a value is pushed
//   dispatch->waiting_reqs[core_id] = req;
//   dispatch->waiting_mask |= 1<<core_id;

//   // Don't forget to remember to clear the event after wake-up by the dispatch event
//   core_eu->clear_evt_mask = 1<<dispatch_event;

//   if (is_caller)
//     return core_eu->wait_event(req);
//   else
//     return core_eu->put_to_sleep(req);
// }



// Dispatch_unit::Dispatch_unit(Event_unit *top)
// : top(top)
// {
//   dispatch_event = top->get_config_int("**/properties/events/dispatch");
//   size = top->get_config_int("**/properties/dispatch/size");
//   core = new Dispatch_core[top->nb_core];
//   dispatches = new Dispatch[size];
// }

//   void Dispatch_unit::reset()
//   {
//     fifo_head = 0;
//     config = 0;
//     for (int i=0; i<top->nb_core; i++)
//     {
//       core[i].tail = 0;
//     }
//     for (int i=0; i<size; i++)
//     {
//       dispatches[i].value = 0;
//       dispatches[i].status_mask = 0;
//       dispatches[i].config_mask = 0;
//       dispatches[i].waiting_mask = 0;
//     }
//   }

//   vp::io_req_status_e Dispatch_unit::req(vp::io_req *req, uint64_t offset, bool is_write, uint32_t *data, int core_id)
//   {
//     if (offset == EU_DISPATCH_FIFO_ACCESS)
//     {
//       if (is_write)
//       {
//         unsigned int id = fifo_head++;
//         if (fifo_head == size) fifo_head = 0;

//         Dispatch *dispatch = &dispatches[id];

//         // When pushing to the FIFO, the global config is pushed to the elected dispatcher
//         dispatch->config_mask = config;     // Cores that will get a valid value

//         top->trace.msg("Pushing dispatch value (dispatch: %d, value: 0x%x, coreMask: 0x%x)\n", id, *data, dispatch->config_mask);

//         // Case where the master push a value
//         dispatch->value = *data;
//         // Reinitialize the status mask to notify a new value is ready
//         dispatch->status_mask = -1;
//         // Then wake-up the waiting cores
//         unsigned int mask = dispatch->waiting_mask & dispatch->status_mask;
//         for (int i=0; i<32 && mask; i++)
//         {
//           if (mask & (1<<i))
//           {
//             // Only wake-up the core if he's actually involved in the team
//             if (dispatch->config_mask & (1<<i))
//             {
//               top->trace.msg("Waking-up core waiting for dispatch value (coreId: %d)\n", i);
//               vp::io_req *waiting_req = dispatch->waiting_reqs[i];

//               // Clear the status bit as the waking core takes the data
//               dispatch->status_mask &= ~(1<<i);
//               dispatch->waiting_mask &= ~(1<<i);

//               // Store the dispatch value into the pending request
//               // Don't reply now to the initiator, this will be done by the wakeup event
//               // to introduce some delays
//               *(uint32_t *)waiting_req->get_data() = dispatch->value;

//               // Update the core fifo
//               core[i].tail++;
//               if (core[i].tail == size) core[i].tail = 0;

//               // Clear the mask to stop iterating early
//               mask &= ~(1<<i);

//               // And trigger the event to the core
//               top->trigger_event(1<<dispatch_event, 0, 1<<i);
//             }
//             // Otherwise keep him sleeping and increase its index so that he will bypass this entry when he wakes up
//             else
//             {
//               // Cancel current dispatch sleep
//               dispatch->status_mask &= ~(1<<i);
//               dispatch->waiting_mask &= ~(1<<i);
//               vp::io_req *pending_req = dispatch->waiting_reqs[i];

//               // Bypass the current entry
//               core[i].tail++;
//               if (core[i].tail == size) core[i].tail = 0;

//               // And reenqueue to the next entry
//               id = core[i].tail;
//               enqueue_sleep(&dispatches[id], pending_req, i, false);
//               top->trace.msg("Incrementing core counter to bypass entry (coreId: %d, newIndex: %d)\n", i, id);
//             }
//           }
//         }

//         return vp::IO_REQ_OK;
//       }
//       else
//       {
//         int id = core[core_id].tail;
//         Dispatch *dispatch = &dispatches[id];

//         top->trace.msg("Trying to get dispatch value (dispatch: %d)\n", id);

//         // In case we found ready elements where this core is not involved, bypass them all
//         while ((dispatch->status_mask & (1<<core_id)) && !(dispatch->config_mask & (1<<core_id))) {
//           dispatch->status_mask &= ~(1<<core_id);
//           core[core_id].tail++;
//           if (core[core_id].tail == size) core[core_id].tail = 0;
//           id = core[core_id].tail;
//           dispatch = &dispatches[id];
//           top->trace.msg("Incrementing core counter to bypass entry (coreId: %d, newIndex: %d)\n", core_id, id);
//         }

//         // Case where a slave tries to get a value
//         if (dispatch->status_mask & (1<<core_id))
//         {
//           // A value is ready. Get it and clear the status bit to not read it again the next time
//           // In case the core is not involved in this dispatch, returns 0
//           if (dispatch->config_mask & (1<<core_id)) *data = dispatch->value;
//           else *data = 0;
//           dispatch->status_mask &= ~(1<<core_id);
//           top->trace.msg("Getting ready dispatch value (dispatch: %d, value: %x, dispatchStatus: 0x%x)\n", id, dispatch->value, dispatch->status_mask);
//           core[core_id].tail++;
//           if (core[core_id].tail == size) core[core_id].tail = 0;
//         }
//         else
//         {
//           // Nothing is ready, go to sleep
//           top->trace.msg("No ready dispatch value, going to sleep (dispatch: %d, value: %x, dispatchStatus: 0x%x)\n", id, dispatch->value, dispatch->status_mask);
//           return enqueue_sleep(dispatch, req, core_id);
//         }

//         return vp::IO_REQ_OK;
//       }

//       return vp::IO_REQ_INVALID;
//     }
//     else if (offset == EU_DISPATCH_TEAM_CONFIG)
//     {
//       config = *data;
//       return vp::IO_REQ_OK;
//     }
//     else
//     {
//       return vp::IO_REQ_INVALID;
//     }
//   }



/****************
 * BARRIER UNIT
 ****************/
Barrier_unit::Barrier_unit(Event_unit *top)
: top(top)
{
  top->traces.new_trace("barrier/trace", &trace, vp::DEBUG);
  nb_barriers = top->get_config_int("**/properties/barriers/nb_barriers");
  barrier_event = top->get_config_int("**/properties/events/barrier");
  barriers = new Barrier[nb_barriers];
}

void Barrier_unit::check_barrier(int barrier_id)
{
  Barrier *barrier = &barriers[barrier_id];

  if (barrier->status == barrier->core_mask)
  {
    trace.msg("Barrier reached, triggering event (barrier: %d, coreMask: 0x%x, targetMask: 0x%x)\n", barrier_id, barrier->core_mask[0], barrier->target_mask[0]);
    barrier->status.fill(0);
    top->trigger_event(1<<barrier_event, barrier->target_mask);
  }
}


vp::io_req_status_e Barrier_unit::req(vp::io_req *req, uint64_t offset, bool is_write, uint32_t *data, int core)
{
  unsigned int barrier_id = EU_BARRIER_AREA_BARRIERID_GET(offset);
  offset = offset - EU_BARRIER_AREA_OFFSET_GET(barrier_id);
  int internal_offset = (offset % EU_MASK_REG_SIZE) >> 2;
  if (barrier_id >= nb_barriers) return vp::IO_REQ_INVALID;
  Barrier *barrier = &barriers[barrier_id];

  if (offset >= EU_HW_BARR_TRIGGER_MASK && offset < EU_HW_BARR_TRIGGER_MASK+EU_MASK_REG_SIZE)
  {
    if (!is_write) *data = barrier->core_mask[internal_offset];
    else {
      trace.msg("Setting barrier core mask (barrier: %d, mask: 0x%x)\n", barrier_id, *data);
      barrier->core_mask[internal_offset] = *data;
      check_barrier(barrier_id);
    }
  }
  else if (offset >= EU_HW_BARR_STATUS && offset < EU_HW_BARR_STATUS+EU_MASK_REG_SIZE)
  {
    if (!is_write) *data = barrier->status[internal_offset];
    else {
      trace.msg("Setting barrier status (barrier: %d, status: 0x%x)\n", barrier_id, *data);
      barrier->status[internal_offset] = *data;
      check_barrier(barrier_id);
    }
  }
  else if (offset >= EU_HW_BARR_STATUS_SUMMARY && offset < EU_HW_BARR_STATUS_SUMMARY+EU_MASK_REG_SIZE)
  {
    if (is_write) return vp::IO_REQ_INVALID;
    uint32_t status = 0;
    for (unsigned int i=1; i<nb_barriers; i++) status |= barriers[i].status[internal_offset];
    *data = status;
  }
  else if (offset >= EU_HW_BARR_TARGET_MASK && offset < EU_HW_BARR_TARGET_MASK+EU_MASK_REG_SIZE)
  {
    if (!is_write) *data = barrier->target_mask[internal_offset];
    else {
      trace.msg("Setting barrier target mask (barrier: %d, mask: 0x%x)\n", barrier_id, *data);
      barrier->target_mask[internal_offset] = *data;
      check_barrier(barrier_id);
    }
  }
  else if (offset >= EU_HW_BARR_TRIGGER && offset < EU_HW_BARR_TRIGGER+EU_MASK_REG_SIZE)
  {
    if (!is_write) return vp::IO_REQ_INVALID;
    else {
      barrier->status[internal_offset] |= *data;
      trace.msg("Barrier mask trigger (barrier: %d, mask: 0x%x, newStatus: 0x%x)\n", barrier_id, *data, barrier->status[internal_offset]);
    }

    check_barrier(barrier_id);
  }
  else if (offset == EU_HW_BARR_TRIGGER_SELF)
  {
    // The access is valid only through the demux
    if (core == -1) return vp::IO_REQ_INVALID;
    barrier->status[core/32] |= 1 << (core % 32);
    trace.msg("Barrier trigger (barrier: %d, coreId: %d, newStatus: 0x%x)\n", barrier_id, core, barrier->status[core/32]);

    check_barrier(barrier_id);
  }
  else if (offset == EU_HW_BARR_TRIGGER_WAIT)
  {
    // The access is valid only through the demux
    if (core == -1) return vp::IO_REQ_INVALID;

    Core_event_unit *core_eu = &top->core_eu[core];
    if (core_eu->get_state() == CORE_STATE_WAITING_BARRIER)
    {
      // The core was already waiting for the barrier which means it was interrupted
      // by an interrupt. Just resume the barrier by going to sleep
      trace.msg("Resuming barrier trigger and wait (barrier: %d, coreId: %d, newStatus: 0x%x)\n", barrier_id, core, barrier->status[core/32]);
    }
    else
    {
      barrier->status[core/32] |= 1 << (core % 32);
      trace.msg("Barrier trigger and wait (barrier: %d, coreId: %d, newStatus: 0x%x)\n", barrier_id, core, barrier->status[core/32]);
    }

    check_barrier(barrier_id);

    return core_eu->wait_event(req, CORE_STATE_WAITING_BARRIER);
  }
  else if (offset == EU_HW_BARR_TRIGGER_WAIT_CLEAR)
  {
    // The access is valid only through the demux
    if (core == -1) return vp::IO_REQ_INVALID;

    Core_event_unit *core_eu = &top->core_eu[core];
    if (core_eu->get_state() == CORE_STATE_WAITING_BARRIER)
    {
      // The core was already waiting for the barrier which means it was interrupted
      // by an interrupt. Just resume the barrier by going to sleep
      trace.msg("Resuming barrier trigger and wait (barrier: %d, coreId: %d, mask: 0x%x, newStatus: 0x%x)\n", barrier_id, core, barrier->core_mask[core/32], barrier->status[core/32]);
    }
    else
    {
      barrier->status[core/32] |= 1 << (core % 32);
      trace.msg("Barrier trigger, wait and clear (barrier: %d, coreId: %d, newStatus: 0x%x)\n", barrier_id, core, barrier->status[core/32]);
    }
    core_eu->clear_evt_mask = core_eu->evt_mask;

    check_barrier(barrier_id);

    return core_eu->wait_event(req, CORE_STATE_WAITING_BARRIER);
  }
  else
  {
    return vp::IO_REQ_INVALID;
  }
  return vp::IO_REQ_OK;
}

void Barrier_unit::reset()
{
  for (int i=0; i<nb_barriers; i++)
  {
    Barrier *barrier = &barriers[i];
    barrier->core_mask.fill(0);
    barrier->status.fill(0);
    barrier->target_mask.fill(0);
  }
}


Soc_event_unit::Soc_event_unit(Event_unit *top) : top(top)
{
  this->nb_fifo_events = top->get_config_int("**/nb_fifo_events");
  this->fifo_soc_event = top->get_config_int("**/fifo_event");

  top->traces.new_trace("soc_eu/trace", &trace, vp::DEBUG);

  this->fifo_event = new int[nb_fifo_events];

  this->soc_event_itf.set_sync_meth(&Soc_event_unit::sync);
  top->new_slave_port(this, "soc_event", &this->soc_event_itf);

  this->reset();
}

void Soc_event_unit::reset()
{
  this->nb_free_events = this->nb_fifo_events;
  this->fifo_event_head = 0;
  this->fifo_event_tail = 0;
}

void Soc_event_unit::check_state()
{
  if (this->fifo_soc_event != -1 && this->nb_free_events != this->nb_fifo_events) {
    this->trace.msg("Generating FIFO event (id: %d)\n", this->fifo_soc_event);
    std::array<uint32_t, EU_MASK_REG_SIZE/4> core_mask;
    core_mask.fill((uint32_t) -1);
    this->top->trigger_event(1<<this->fifo_soc_event, core_mask);
  }
}

void Soc_event_unit::sync(void *__this, int event)
{
  Soc_event_unit *_this = (Soc_event_unit *)__this;
  _this->trace.msg("Received soc event (event: %d)\n", event);

  if (_this->nb_free_events == 0) {
    return;
  }

  _this->nb_free_events--;
  _this->fifo_event[_this->fifo_event_head] = event;
  _this->fifo_event_head++;
  if (_this->fifo_event_head == _this->nb_fifo_events) _this->fifo_event_head = 0;

  _this->check_state();
}

vp::io_req_status_e Soc_event_unit::ioReq(uint32_t offset, bool is_write, uint32_t *data)
{
  if (is_write) return vp::IO_REQ_INVALID;

  if (nb_free_events == nb_fifo_events) {
    trace.msg("Reading FIFO with no event\n");
    *data = 0;
  } else {
    trace.msg("Popping event from FIFO (id: %d)\n", fifo_event[fifo_event_tail]);
    *data = (1 << EU_SOC_EVENTS_VALID_BIT) | fifo_event[fifo_event_tail];
    fifo_event_tail++;
    if (fifo_event_tail == nb_fifo_events) fifo_event_tail = 0;
    nb_free_events++;
    if (nb_free_events != nb_fifo_events)
    {
      this->trace.msg("Generating FIFO soc event (id: %d)\n", this->fifo_soc_event);
      std::array<uint32_t, EU_MASK_REG_SIZE/4> core_mask;
      core_mask.fill((uint32_t) -1);
      this->top->trigger_event(1<<this->fifo_soc_event, core_mask);
    }
  }

  return vp::IO_REQ_OK;
}