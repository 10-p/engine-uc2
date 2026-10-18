# System Link helper for m10-inject.py scenarios (pyfile): ask a listen server's System Link beacon for its text, as UC2's
# "Join System Link Game" page does natively (IpDrv.ClientBeaconReceiver -> IpDrv.UdpBeacon.ReceivedText "REPORT?<nonce>"
# -> BroadcastBeacon: "<BeaconProduct> <game port> ?BeaconText=<GameInfo.GetBeaconText()>?Nonce=<nonce>").
# BEACON = the reply's beacon text split on '|' (game type, server name, map, maplist, players, max players, purity,
# mutators, player names (';'), handicap); BEACON_RAW = the whole reply.
import socket

BEACON, BEACON_RAW = [], ''

def beacon(port, tries=5):
    global BEACON, BEACON_RAW
    for _ in range(tries):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(2.0)
        try:
            s.sendto(b'REPORT?4242', ('127.0.0.1', int(port)))
            data, addr = s.recvfrom(8192)
        except OSError as e:
            print('  beacon: no reply on udp', port, repr(e))
            continue
        finally:
            s.close()
        BEACON_RAW = data.decode('latin-1').rstrip('\x00')
        text = BEACON_RAW.split('?BeaconText=', 1)[1] if '?BeaconText=' in BEACON_RAW else ''
        text = text.split('?Nonce=', 1)[0]
        BEACON = text.split('|')
        print('  beacon reply from %s:%d: %s' % (addr[0], addr[1], BEACON_RAW))
        return BEACON
    return BEACON
