import unittest
from mirror import Grant, Request, Route, decide, dispatch, MAX_COPY

def req(n, *a, payload=b""):
    return Request(n, tuple((list(a)+[0]*6)[:6]), payload)

class SecurityTests(unittest.TestCase):
    def setUp(self):
        self.grant=Grant(7,fs_read=True,net_connect=True)
        self.calls=[]
        self.service=lambda b,r: self.calls.append((b,r)) or 123

    def test_authorized_readonly_open(self):
        self.assertEqual(dispatch({7:self.grant},7,req(257,-100,0,10,payload=b"/site/file"),self.service),123)
        self.assertEqual(len(self.calls),1)

    def test_badge_not_controlled_by_caller(self):
        self.assertEqual(dispatch({7:self.grant},8,req(1,1,0,0),self.service),-1)
        self.assertEqual(dispatch({7:Grant(8)},7,req(1,1,0,0),self.service),-1)
        self.assertEqual(self.calls,[])

    def test_missing_fs_grant(self):
        self.assertEqual(decide(Grant(8),req(257,-100,0,10,payload=b"/site/file")).errno,13)

    def test_fs_write_denied(self):
        self.assertEqual(decide(self.grant,req(257,-100,1,10,payload=b"/site/file")).route,Route.DENIED)

    def test_path_snapshot_validation(self):
        for payload in (b"/foo\x00/bar", b"a"*256):
            self.assertEqual(decide(self.grant,req(257,-100,0,len(payload),payload=payload)).route,Route.DENIED)
        self.assertEqual(decide(self.grant,req(257,-100,0,20,payload=b"/site/file")).errno,22)

    def test_bounded_write(self):
        self.assertEqual(decide(self.grant,req(1,1,0,MAX_COPY+1,payload=b"a")).errno,14)
        self.assertEqual(decide(self.grant,req(1,1,0,1,payload=b"")).errno,14)

    def test_mutable_payload_rejected(self):
        self.assertEqual(decide(self.grant,Request(1,(1,0,1,0,0,0),bytearray(b"a"))).route,Route.DENIED)

    def test_network_grants(self):
        self.assertEqual(decide(Grant(8),req(41,2,1)).errno,13)
        self.assertEqual(decide(self.grant,req(49,3)).errno,13)
        self.assertEqual(decide(self.grant,req(42,3)).route,Route.MEDIATED)

    def test_escapes_are_denied(self):
        for n in (56,59,101,165,169,298,321):
            self.assertEqual(decide(self.grant,req(n)).errno,1)
        self.assertEqual(decide(self.grant,req(999)).errno,38)

    def test_local_never_forwarded(self):
        self.assertEqual(dispatch({7:self.grant},7,req(9),self.service),-38)
        self.assertEqual(self.calls,[])

    def test_malformed_args(self):
        self.assertEqual(decide(self.grant,Request(1,(1,),b"")).errno,22)

if __name__=="__main__":
    unittest.main()
