#!/usr/bin/python
from compatibility import any
from unittest import TestCase, main
from common import createClientWithCerts, startNufw, connectClient
from inl_tests.iptables import Iptables
from config import USERNAME, PASSWORD
from nuauth import Nuauth
from nuauth_conf import NuauthConf
from plaintext import PlaintextUserDB, PlaintextUser, PlaintextAcl
from time import sleep
from filter import testAllowPort, VALID_PORT, HOST
from mysocket import connectTcp

DELAY = 2
TIMEOUT = 2.0
DURATION = 3

class TestSessionExpire(TestCase):
    def setUp(self):
        self.expiration = DURATION
        self.host = HOST

        # Setup session_expire library
        nuconfig = NuauthConf()
        nuconfig['nuauth_user_session_modify_module']='"session_expire"'
        nuconfig['nuauth_session_duration'] = str(self.expiration)

        # Install temporary user database
        self.userdb = PlaintextUserDB()
        self.userdb.addUser( PlaintextUser(USERNAME, PASSWORD, 42, 42) )
        self.userdb.install(nuconfig)
        self.acls = PlaintextAcl()

        # Start nuauth
        self.nuauth = Nuauth(nuconfig)

        # Create client
        self.client = createClientWithCerts()

    def tearDown(self):
        self.client.stop()
        self.acls.desinstall()
        self.nuauth.stop()

    def testExpire(self):
        self.assert_(connectClient(self.client))

        sleep(self.expiration+DELAY)

        self.assert_(self.get_session_not_connected())

    def get_session_not_connected(self):
        for line in self.client.readlines(total_timeout=TIMEOUT):
            if line.lower().find('session not connected') >= 0:
                return True
        return False

if __name__ == "__main__":
    print "Test nuauth client authentication"
    main()

