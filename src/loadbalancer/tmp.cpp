

opt.null();
opt["short"] = "l";
opt["long"] = "loadbalancer";
opt["help"] = "start load balancer";
conf.addOption("loadbalancer", opt);

opt["arg"] = "string";
opt["short"] = "M";
opt["long"] = "mesh location";
opt["help"] = "the location of the already existing mesh";
conf.addOption("mesh", opt);

opt["arg"] = "integer";
opt["short"] = "p";
opt["long"] = "port";
opt["help"] = "TCP port to listen on";
opt["value"].append(8042u);
conf.addOption("LBport", opt);

opt["arg"] = "string";
opt["short"] = "P";
opt["long"] = "passphrase";
opt["help"] = "Passphrase (prometheus option value) to use for data retrieval.";
opt["value"][0u] = "koekjes";
conf.addOption("passphrase", opt);

opt["arg"] = "string";
opt["short"] = "i";
opt["long"] = "interface";
opt["help"] = "Network interface to listen on";
opt["value"][0u] = "0.0.0.0";
conf.addOption("interface", opt);

opt["arg"] = "string";
opt["short"] = "F";
opt["long"] = "fallback";
opt["help"] = "Default reply if no servers are available";
opt["value"][0u] = "FULL";
conf.addOption("LBfallback", opt);

opt["arg"] = "string";
opt["short"] = "A";
opt["long"] = "auth";
opt["help"] = "load balancer authentication key";
conf.addOption("auth", opt);

opt.null();
opt["short"] = "L";
opt["long"] = "localmode";
opt["help"] = "Control only from local interfaces, request balance from all";
conf.addOption("localmode", opt);

opt.null();
opt["short"] = "c";
opt["long"] = "config";
opt["help"] = "load config settings from file";
conf.addOption("load", opt);

opt["arg"] = "string";
opt["short"] = "H";
opt["long"] = "host";
opt["help"] = "Host name and port where this load balancer can be reached";
conf.addOption("LBName", opt);

std::string mesh = conf.getString("mesh");
bool load = conf.getBool("loadbalancer");
std::string host = getHost();
std::string LBhost = conf.getString("LBName");
std::string LBauth = conf.getString("auth");
std::string LBport = conf.getInt("LBport");
std::string LBinterface = conf.getString("LBinterface");
std::string LBFallback = conf.getString("LBfallback");
bool LBlocalmode = conf.getOption("localmode");
bool LBLoad = conf.getOption("load");

if (mesh.size()){
  if (load){// add load balancer to mesh
    HTTP::URL url(mesh);
    url.protocol = "http";
    if (!url.port.size()){url.port = "8042";}
    url.path.clear();
    url.path = "/loadbalancers/" + LBhost;
    HTTP::Downloader DL;
    if (DL.get(url) && DL.isOk()){INFO_MSG("load balancer started");}
  }
}else{// take new lb host as mesh if none defined
  mesh = LBhost;
}
// add server to mesh
HTTP::URL url(mesh);
url.protocol = "http";
if (!url.port.size()){url.port = "8042";}
url.path.clear();
url.path = "/servers/" + host;
HTTP::Downloader DL;
if (DL.get(url) && DL.isOk()){INFO_MSG("load balancer started");}
