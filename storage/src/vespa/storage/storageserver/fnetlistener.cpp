// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#include "fnetlistener.h"
#include "communicationmanager.h"
#include "rpcrequestwrapper.h"
#include <vespa/storageapi/message/state.h>
#include <vespa/vespalib/util/exceptions.h>

#include <vespa/log/log.h>

LOG_SETUP(".rpc.listener");

namespace storage {


FNetListener::FNetListener(
        CommunicationManager& comManager, const config::ConfigUri & configUri, uint32_t port)
    : _comManager(comManager),
      _orb(),
      _closed(false),
      _slobrokRegister(_orb, configUri)
{
    initRPC();
    if (!_orb.Listen(port)) {
        std::ostringstream ost;
        ost << "Failed to listen to RPC port " << port << ".";
        throw vespalib::IllegalStateException(ost.str(), VESPA_STRLOC);
    }
    _orb.Start();
}

FNetListener::~FNetListener()
{
    if (!_closed) {
        close();
    }
}

void
FNetListener::registerHandle(const vespalib::stringref & handle) {
    _slobrokRegister.registerName(handle);
    while (_slobrokRegister.busy()) {
        LOG(debug, "Waiting to register in slobrok");
        FastOS_Thread::Sleep(50);
    }
    _handle = handle;
}

void
FNetListener::close()
{
    _closed = true;
    _slobrokRegister.unregisterName(_handle);
    _orb.ShutDown(true);
}

void
FNetListener::initRPC()
{
    FRT_ReflectionBuilder rb(&_orb);

    rb.DefineMethod(
            "getnodestate3", "sii", "ss", true,
            FRT_METHOD(FNetListener::RPC_getNodeState2),
            this);
    rb.MethodDesc("Get state of this node");
    rb.ParamDesc("nodestate", "Expected state of given node. If correct, the "
            "request will be queued on target until it changes. To not give "
            "any state use the string 'unknown', enforcing a direct reply.");
    rb.ParamDesc("timeout", "Timeout of message in milliseconds, set by the "
            "state requester");
    rb.ReturnDesc("nodestate", "State string for this node");
    rb.ReturnDesc("hostinfo", "Information about host this node is running on");
    //-------------------------------------------------------------------------
    rb.DefineMethod(
            "getnodestate2", "si", "s", true,
            FRT_METHOD(FNetListener::RPC_getNodeState2),
            this);
    rb.MethodDesc("Get state of this node");
    rb.ParamDesc("nodestate", "Expected state of given node. If correct, the "
            "request will be queued on target until it changes. To not give "
            "any state use the string 'unknown', enforcing a direct reply.");
    rb.ParamDesc("timeout", "Timeout of message in milliseconds, set by the "
            "state requester");
    rb.ReturnDesc("nodestate", "State string for this node");
    //-------------------------------------------------------------------------
    rb.DefineMethod(
            "setsystemstate2", "s", "", true,
            FRT_METHOD(FNetListener::RPC_setSystemState2),
            this);
    rb.MethodDesc("Set systemstate on this node");
    rb.ParamDesc("systemstate", "New systemstate to set");
    //-------------------------------------------------------------------------
    rb.DefineMethod(
            "getcurrenttime", "", "lis", true,
            FRT_METHOD(FNetListener::RPC_getCurrentTime),
            this);
    rb.MethodDesc("Get current time on this node");
    rb.ReturnDesc("seconds", "Current time in seconds since epoch");
    rb.ReturnDesc("nanoseconds", "additional nanoseconds since epoch");
    rb.ReturnDesc("hostname", "Host name");
    //-------------------------------------------------------------------------
}


void
FNetListener::RPC_getCurrentTime(FRT_RPCRequest *req)
{
    if (_closed) {
        LOG(debug, "Not handling RPC call getCurrentTime() as we have closed");
        req->SetError(RPCRequestWrapper::ERR_NODE_SHUTTING_DOWN, "Node shutting down");
        return;
    }
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    req->GetReturn()->AddInt64(t.tv_sec);
    req->GetReturn()->AddInt32(t.tv_nsec);
    vespalib::string hostname = FastOS_Socket::getHostName();
    req->GetReturn()->AddString(hostname.c_str());
    // all handled, will return immediately
    return;
}

void
FNetListener::RPC_getNodeState2(FRT_RPCRequest *req)
{
    if (_closed) {
        LOG(debug, "Not handling RPC call getNodeState2() as we have closed");
        req->SetError(RPCRequestWrapper::ERR_NODE_SHUTTING_DOWN, "Node shutting down");
        return;
    }

    vespalib::string expected(req->GetParams()->GetValue(0)._string._str,
                         req->GetParams()->GetValue(0)._string._len);

    std::shared_ptr<api::GetNodeStateCommand> cmd(
            new api::GetNodeStateCommand(
                  expected != "unknown" ?
                    std::unique_ptr<lib::NodeState>(new lib::NodeState(expected)) :
                    std::unique_ptr<lib::NodeState>()));

    cmd->setPriority(api::StorageMessage::VERYHIGH);
    cmd->setTimeout(req->GetParams()->GetValue(1)._intval32);
    if (req->GetParams()->GetNumValues() > 2) {
        cmd->setSourceIndex(req->GetParams()->GetValue(2)._intval32);
    }
        // Create a request object to avoid needing a separate transport type
    std::unique_ptr<RPCRequestWrapper> request(new RPCRequestWrapper(req));
    cmd->setTransportContext(std::unique_ptr<api::TransportContext>(
            new StorageTransportContext(std::move(request))));
    req->Detach();
    _comManager.enqueue(cmd);
}

void
FNetListener::RPC_setSystemState2(FRT_RPCRequest *req)
{
    if (_closed) {
        LOG(debug, "Not handling RPC call setSystemState2() as we have closed");
        req->SetError(RPCRequestWrapper::ERR_NODE_SHUTTING_DOWN, "Node shutting down");
        return;
    }
    vespalib::string systemStateStr(req->GetParams()->GetValue(0)._string._str,
                               req->GetParams()->GetValue(0)._string._len);
    lib::ClusterState systemState(systemStateStr);

    std::shared_ptr<api::SetSystemStateCommand> cmd(
            new api::SetSystemStateCommand(systemState));
    cmd->setPriority(api::StorageMessage::VERYHIGH);

        // Create a request object to avoid needing a separate transport type
    std::unique_ptr<RPCRequestWrapper> request(new RPCRequestWrapper(req));
    cmd->setTransportContext(std::unique_ptr<api::TransportContext>(
            new StorageTransportContext(std::move(request))));
    req->Detach();
    _comManager.enqueue(cmd);
}



}
