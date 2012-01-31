
#include <iostream>
#include <boost/bind.hpp>
#include <time.h>

#include <Swiften/Swiften.h>

using namespace Swift;
using namespace boost;


const char SVPN_PREFIX[] = "SVPN";

class SvpnIQHandler : public GetResponder<Body> {

  public:
    SvpnIQHandler(IQRouter* router) : GetResponder<Body>(router) {}

  private:
    virtual bool handleGetRequest(const JID& from, const JID& to, 
      const std::string& id, boost::shared_ptr<Body> msg) {
      std::cout << from.toString() << " " << msg->getText() << std::endl;
      return true;
    }

};

extern "C" void testPrint() {

  std::cout << "I'm testing this bloadclot" << std::endl;

}

void sendIQ(Client* client, const JID& jid) {

  shared_ptr<Body> msg(new Body("addr1fpr1"));
  IQ::ref iq = IQ::createRequest(IQ::Get, jid, "id1", msg);

  client->getIQRouter()->sendIQ(iq);
}

void handlePresence(Client* client, Presence::ref presence) {
  std::cout <<  "status = " << presence->getStatus() << std::endl;

  const JID& jid = presence->getFrom();
  std::cout << "resource " << jid.getResource() << std::endl;

  std::cout << jid.getResource().compare(0,4,"SVPN") << std::endl;

  if (jid.getResource().compare(0,4,"SVPN") == 0) {
    std::cout << "svpn found" << std::endl;
    sendIQ(client, jid);
  }
}

void handleRoster(XMPPRoster* roster, Client* client) {

  std::vector<XMPPRosterItem> friends = roster->getItems();
  std::vector<XMPPRosterItem>::const_iterator it;

  for (it = friends.begin(); it != friends.end(); it++) {
    const XMPPRosterItem item = *it;
    const JID& jid = item.getJID();

    std::cout << jid.toString() << std::endl;

    const std::vector<std::string>& groups = item.getGroups();
    std::vector<std::string>::const_iterator cit;
    for (cit = groups.begin(); cit != groups.end(); cit++) {
      std::cout << *cit << std::endl;
    }
  }

}

void handleConnected(Client* client) {

  std::cout << "connected" << std::endl;

  XMPPRoster*  roster = client->getRoster();
  roster->onInitialRosterPopulated.connect(bind(&handleRoster, roster, client));
  client->requestRoster();

  Presence::ref pres(new Presence("hello good morning"));
  pres->setFrom(client->getJID());
  client->sendPresence(pres);
}

int main(int argc, char* argv[]) {

  if (argc < 3) {
    std::cout << "usage: ./xmppnetwork jabberid password" << std::endl;
    return 0;
  }

  srand(time(NULL));

  std::stringstream out;
  out << SVPN_PREFIX << rand();
  const std::string resource = out.str();

  JID jid("ptony82", "jabber.org", resource);

  SimpleEventLoop eventLoop;
  BoostNetworkFactories networkFactories(&eventLoop);

  Client* client = new Client(jid, argv[2], &networkFactories);
  ClientXMLTracer tracer(client);

  client->setAlwaysTrustCertificates();
  client->onConnected.connect(bind(&handleConnected, client));
  client->onPresenceReceived.connect(bind(&handlePresence, client, _1));
  client->connect();

  eventLoop.run();

  delete client;
  return 0;

}

