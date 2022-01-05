import grpc
import logging
from spdk.rpc.client import JSONRPCException
from .device import DeviceManager, DeviceException
from ..common import format_volume_id
from ..proto import sma_pb2
from ..proto import nvmf_tcp_pb2


class NvmfTcpDeviceManager(DeviceManager):
    def __init__(self, client):
        super().__init__('nvmf_tcp', client)

    def init(self, config):
        self._has_transport = self._create_transport()

    def _create_transport(self):
        try:
            with self._client() as client:
                transports = client.call('nvmf_get_transports')
                for transport in transports:
                    if transport['trtype'].lower() == 'tcp':
                        return True
                # TODO: take the transport params from config
                return client.call('nvmf_create_transport',
                                   {'trtype': 'tcp'})
        except JSONRPCException:
            logging.error('Failed to query for NVMe/TCP transport')
            return False

    def _check_transport(f):
        def wrapper(self, *args):
            if not self._has_transport:
                raise DeviceException(grpc.StatusCode.INTERNAL,
                                      'NVMe/TCP transport is unavailable')
            return f(self, *args)
        return wrapper

    def _get_params(self, request, params):
        result = {}
        for grpc_param, *rpc_param in params:
            rpc_param = rpc_param[0] if rpc_param else grpc_param
            result[rpc_param] = getattr(request, grpc_param)
        return result

    def _check_addr(self, addr, addrlist):
        return next(filter(lambda a: (
            a['trtype'].lower() == 'tcp' and
            a['adrfam'].lower() == addr['adrfam'].lower() and
            a['traddr'].lower() == addr['traddr'].lower() and
            a['trsvcid'].lower() == addr['trsvcid'].lower()), addrlist), None) is not None

    @_check_transport
    def create_device(self, request):
        params = request.nvmf_tcp
        with self._client() as client:
            try:
                subsystems = client.call('nvmf_get_subsystems')
                for subsystem in subsystems:
                    if subsystem['nqn'] == params.subnqn:
                        break
                else:
                    subsystem = None
                    result = client.call('nvmf_create_subsystem',
                                         {**self._get_params(params, [
                                                ('subnqn', 'nqn'),
                                                ('allow_any_host',)])})
            except JSONRPCException:
                raise DeviceException(grpc.StatusCode.INTERNAL,
                                      'Failed to create NVMe/TCP device')
            try:
                for host in params.hosts:
                    client.call('nvmf_subsystem_add_host',
                                {'nqn': params.subnqn,
                                 'host': host})
                if subsystem is not None:
                    for host in [h['nqn'] for h in subsystem['hosts']]:
                        if host not in params.hosts:
                            client.call('nvmf_subsystem_remove_host',
                                        {'nqn': params.subnqn,
                                         'host': host})

                addr = self._get_params(params, [
                                ('adrfam',),
                                ('traddr',),
                                ('trsvcid',)])
                if subsystem is None or not self._check_addr(addr,
                                                             subsystem['listen_addresses']):
                    client.call('nvmf_subsystem_add_listener',
                                {'nqn': params.subnqn,
                                 'listen_address': {'trtype': 'tcp', **addr}})
                volume_id = format_volume_id(request.volume.volume_id)
                if volume_id is not None:
                    result = client.call('nvmf_subsystem_add_ns',
                                         {'nqn': params.subnqn,
                                          'namespace': {
                                              'bdev_name': volume_id}})
            except JSONRPCException:
                try:
                    client.call('nvmf_delete_subsystem', {'nqn': params.subnqn})
                except JSONRPCException:
                    logging.warning(f'Failed to delete subsystem: {params.subnqn}')
                raise DeviceException(grpc.StatusCode.INTERNAL,
                                      'Failed to create NVMe/TCP device')

        return sma_pb2.CreateDeviceResponse(handle=f'nvmf-tcp:{params.subnqn}')