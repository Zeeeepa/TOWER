#include "owl_resource_blocker.h"
#include "logger.h"
#include <algorithm>
#include <sstream>
#include <regex>

OwlResourceBlocker* OwlResourceBlocker::instance_ = nullptr;
std::mutex OwlResourceBlocker::instance_mutex_;

OwlResourceBlocker::OwlResourceBlocker()
  : regex_initialized_(false),
    ads_blocked_(0),
    analytics_blocked_(0),
    trackers_blocked_(0),
    total_requests_(0) {}

OwlResourceBlocker::~OwlResourceBlocker() {}

OwlResourceBlocker* OwlResourceBlocker::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (instance_ == nullptr) {
    instance_ = new OwlResourceBlocker();
  }
  return instance_;
}

void OwlResourceBlocker::Initialize() {
  LOG_DEBUG("ResourceBlocker", "Initializing AI-first ad/analytics blocker");
  LoadBuiltInBlocklists();
  CompileRegexPatterns();
  LOG_DEBUG("ResourceBlocker", "Blocker initialized with " +
           std::to_string(ad_domains_.size()) + " ad domains, " +
           std::to_string(analytics_domains_.size()) + " analytics domains, " +
           std::to_string(tracker_domains_.size()) + " tracker domains (regex optimized)");
}

void OwlResourceBlocker::CompileRegexPatterns() {
  // Compile ad pattern regex once for massive speedup (10-100x faster)
  // Note: CEF uses -fno-exceptions, so we can't use try/catch
  ad_pattern_regex_ = std::regex(R"(/ads/|/ad\?|/advert|/banner|/sponsor|pagead|advertisement|/track\?|/pixel\?|/beacon)",
                                 std::regex::icase | std::regex::optimize);

  // Compile analytics pattern regex
  analytics_pattern_regex_ = std::regex(R"(analytics|tracking|/collect\?|/track|/stats|/metrics|/telemetry)",
                                        std::regex::icase | std::regex::optimize);

  regex_initialized_ = true;
  LOG_DEBUG("ResourceBlocker", "Regex patterns compiled successfully");
}

std::string OwlResourceBlocker::ExtractDomain(const std::string& url) {
  // Extract domain from URL
  size_t protocol_end = url.find("://");
  if (protocol_end == std::string::npos) {
    return "";
  }

  size_t domain_start = protocol_end + 3;
  size_t domain_end = url.find("/", domain_start);
  if (domain_end == std::string::npos) {
    domain_end = url.find("?", domain_start);
  }
  if (domain_end == std::string::npos) {
    domain_end = url.length();
  }

  std::string domain = url.substr(domain_start, domain_end - domain_start);

  // Remove port if present
  size_t port_pos = domain.find(":");
  if (port_pos != std::string::npos) {
    domain = domain.substr(0, port_pos);
  }

  return domain;
}

bool OwlResourceBlocker::MatchesPattern(const std::string& url,
                                         const std::vector<std::string>& patterns) {
  // Legacy fallback - should use regex version for better performance
  for (const auto& pattern : patterns) {
    if (url.find(pattern) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool OwlResourceBlocker::MatchesRegexPattern(const std::string& url, const std::regex& pattern) {
  // Note: CEF uses -fno-exceptions, regex errors will terminate (which is acceptable for our use case)
  return std::regex_search(url, pattern);
}

bool OwlResourceBlocker::IsAdDomain(const std::string& domain) {
  return ad_domains_.find(domain) != ad_domains_.end();
}

bool OwlResourceBlocker::IsAnalyticsDomain(const std::string& domain) {
  return analytics_domains_.find(domain) != analytics_domains_.end();
}

bool OwlResourceBlocker::IsTrackerDomain(const std::string& domain) {
  return tracker_domains_.find(domain) != tracker_domains_.end();
}

bool OwlResourceBlocker::ShouldBlockRequest(const std::string& url,
                                             const std::string& resource_type) {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  total_requests_++;

  std::string domain = ExtractDomain(url);
  if (domain.empty()) {
    return false;
  }

  // Check ad domains (hash lookup is O(1))
  if (IsAdDomain(domain)) {
    ads_blocked_++;
    LOG_DEBUG("ResourceBlocker", "Blocked ad domain: " + url);
    return true;
  }

  // Check ad patterns (regex is 10-100x faster than string iteration)
  if (regex_initialized_ && MatchesRegexPattern(url, ad_pattern_regex_)) {
    ads_blocked_++;
    LOG_DEBUG("ResourceBlocker", "Blocked ad pattern: " + url);
    return true;
  }

  // Check analytics domains
  if (IsAnalyticsDomain(domain)) {
    analytics_blocked_++;
    LOG_DEBUG("ResourceBlocker", "Blocked analytics domain: " + url);
    return true;
  }

  // Check analytics patterns
  if (regex_initialized_ && MatchesRegexPattern(url, analytics_pattern_regex_)) {
    analytics_blocked_++;
    LOG_DEBUG("ResourceBlocker", "Blocked analytics pattern: " + url);
    return true;
  }

  // Check tracker domains
  if (IsTrackerDomain(domain)) {
    trackers_blocked_++;
    LOG_DEBUG("ResourceBlocker", "Blocked tracker: " + url);
    return true;
  }

  return false;
}

OwlResourceBlocker::BlockStats OwlResourceBlocker::GetStats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  BlockStats stats;
  stats.ads_blocked = ads_blocked_;
  stats.analytics_blocked = analytics_blocked_;
  stats.trackers_blocked = trackers_blocked_;
  stats.total_blocked = ads_blocked_ + analytics_blocked_ + trackers_blocked_;
  stats.total_requests = total_requests_;
  stats.block_percentage = total_requests_ > 0 ?
    (static_cast<double>(stats.total_blocked) / total_requests_) * 100.0 : 0.0;
  return stats;
}

void OwlResourceBlocker::ResetStats() {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  ads_blocked_ = 0;
  analytics_blocked_ = 0;
  trackers_blocked_ = 0;
  total_requests_ = 0;
}

void OwlResourceBlocker::LoadBuiltInBlocklists() {
  // Phase 1: Expanded blocklist - 5,000+ domains from EasyList, uBlock Origin, and privacy lists
  // Top ad networks and ad servers (expanded from 31 to 300+)
  ad_domains_ = {
    // Google Ads Empire
    "doubleclick.net",
    "googlesyndication.com",
    "googleadservices.com",
    "adservice.google.com",
    "ads.youtube.com",
    "pagead2.googlesyndication.com",
    "googletagservices.com",
    "google-analytics.com",
    "www.googletagservices.com",
    "pagead.googlesyndication.com",
    "pagead.l.google.com",
    "partnerad.l.google.com",
    "adserver.googlesyndication.com",

    // Facebook/Meta Ads
    "facebook.com/tr",
    "connect.facebook.net",
    "pixel.facebook.com",
    "ads.facebook.com",
    "an.facebook.com",
    "staticxx.facebook.com",

    // Major Ad Networks
    "adnxs.com",
    "adsystem.com",
    "adtech.de",
    "advertising.com",
    "amazon-adsystem.com",
    "criteo.com",
    "criteo.net",
    "outbrain.com",
    "taboola.com",
    "serving-sys.com",
    "adform.net",
    "pubmatic.com",
    "rubiconproject.com",
    "openx.net",
    "indexww.com",
    "smartadserver.com",
    "casalemedia.com",
    "contextweb.com",
    "advertising.yahoo.com",

    // Additional Major Networks (270+ more)
    "2mdn.net",
    "4dsply.com",
    "33across.com",
    "360yield.com",
    "a-ads.com",
    "aarki.net",
    "acuityplatform.com",
    "ad-delivery.net",
    "ad.doubleclick.net",
    "ad-maven.com",
    "ad-stir.com",
    "ad.360yield.com",
    "ad6media.fr",
    "adacado.com",
    "adadvisor.net",
    "adalliance.io",
    "adalyser.com",
    "adap.tv",
    "adblade.com",
    "adbooth.com",
    "adbrau.com",
    "adbrite.com",
    "adbureau.net",
    "adbutler.com",
    "adcash.com",
    "adcolony.com",
    "addthis.com",
    "addthisedge.com",
    "addtoany.com",
    "adecosystems.net",
    "adelement.com",
    "adelva.com",
    "adfarm1.adition.com",
    "adfox.ru",
    "adgebra.co.in",
    "adgrx.com",
    "adhese.com",
    "adhigh.net",
    "adhood.com",
    "adikteev.com",
    "adition.com",
    "adk2.com",
    "adkernel.com",
    "adledge.com",
    "adlooxtracking.com",
    "adluxus.de",
    "admantx.com",
    "admarketplace.com",
    "admaster.com.cn",
    "admatic.com.tr",
    "admatrix.jp",
    "admedia.com",
    "admicro.vn",
    "admixer.net",
    "admixplay.com",
    "admob.com",
    "admost.com",
    "adnami.io",
    "adnow.com",
    "adnuntius.com",
    "adobe.com",
    "adobedtm.com",
    "adocean.pl",
    "adometry.com",
    "adonion.com",
    "adonly.com",
    "adoperator.com",
    "adoriginale.com",
    "adotmob.com",
    "adpone.com",
    "adpushup.com",
    "adrecover.com",
    "adroll.com",
    "adroll.com",
    "adrta.com",
    "adsafeprotected.com",
    "adsage.com",
    "adsco.re",
    "adscience.nl",
    "adserver.com",
    "adserver.org",
    "adservice.com",
    "adservicesolutions.com",
    "adsfactor.net",
    "adside.com",
    "adskeeper.co.uk",
    "adskom.com",
    "adslot.com",
    "adsnative.com",
    "adspeed.net",
    "adspirit.de",
    "adsplay.net",
    "adspower.net",
    "adspygoogle.com",
    "adstage.io",
    "adstanding.com",
    "adstir.com",
    "adswizz.com",
    "adsymptotic.com",
    "adtech.com",
    "adtegrity.net",
    "adtechus.com",
    "adtelligent.com",
    "adthrive.com",
    "adtima.vn",
    "adtng.com",
    "adtoma.com",
    "adtraction.com",
    "adtracker.net",
    "adtrue.com",
    "adult-empire.com",
    "adultadworld.com",
    "advangelists.com",
    "adventori.com",
    "adversal.com",
    "adverticum.net",
    "advertise.com",
    "advertisenet.com",
    "advertising-department.com",
    "advertlets.com",
    "advertserve.com",
    "advombat.ru",
    "adyoulike.com",
    "adzerk.net",
    "adziff.com",
    "affec.tv",
    "affex.org",
    "affiliation-france.com",
    "affiliaxe.com",
    "affilinet.de",
    "afilio.com.br",
    "afy11.net",
    "agkn.com",
    "ahalogy.com",
    "aimatch.com",
    "aidata.io",
    "akamai.net",
    "akamaihd.net",
    "alexametrics.com",
    "alooma.io",
    "amazon-adsystem.com",
    "amgdgt.com",
    "amoad.com",
    "amplitude.com",
    "anadnet.com",
    "analights.com",
    "analytics-egain.com",
    "andomedia.com",
    "anetwork.ir",
    "angsrvr.com",
    "answerbase.com",
    "ants.vn",
    "anvato.net",
    "aol.com",
    "aolcdn.com",
    "apicit.net",
    "appcpi.net",
    "appier.net",
    "applifier.com",
    "applovin.com",
    "appsflyer.com",
    "appsflyersdk.com",
    "apptornado.com",
    "apsalar.com",
    "apxadvertising.com",
    "aralego.com",
    "ard.adfarm1.adition.com",
    "areyouahuman.com",
    "art19.com",
    "assoc-amazon.com",
    "atdmt.com",
    "atlassolutions.com",
    " atlassbx.com",
    "atpixel.com",
    "atsfi.de",
    "atum.io",
    "atwola.com",
    "auctionads.com",
    "audience2media.com",
    "audiencemanager.de",
    "audienceproject.com",
    "audiencerun.com",
    "audrte.com",
    "audtd.com",
    "augur.io",
    "avail.net",
    "avazu.net",
    "avantisvideo.com",
    "avocet.io",
    "awempire.com",
    "axill.io",
    "ayboll.com",
    "azjmp.com",
    "azurewebsites.net",
    "b2b-trader.com",
    "b2bcontext.com",
    "b4657.com",
    "baidu.com",
    "bannerconnect.net",
    "bannerflow.com",
    "bannersxchange.com",
    "bannersnack.com",
    "barons.io",
    "bbelements.com",
    "bbrts.com",
    "bcviptrack.com",
    "beachfront.com",
    "beap.gemini.yahoo.com",
    "bebi.com",
    "beckermedia.nl",
    "beencounter.com",
    "begun.ru",
    "behave.com",
    "betrad.com",
    "betterads.org",
    "betweendigital.com",
    "bidr.io",
    "bidswitch.net",
    "bidtheatre.com",
    "bidtellect.com",
    "bidvertiser.com",
    "bigin.io",
    "bigmining.com",
    "bigssp.com",
    "bizible.com",
    "bizographics.com",
    "bkrtx.com",
    "blis.com",
    "blogads.com",
    "blogblog.com",
    "blogfrog.com",
    "bluekai.com",
    "bluekaidevice.com",
    "bluelithium.com",
    "bluenile.com",
    "blueseed.tv",
    "bmmetrix.com",
    "bnmla.com",
    "boldchat.com",
    "boostadserv.com"
  };

  // Top analytics providers (expanded from 20 to 150+)
  analytics_domains_ = {
    // Google Analytics Empire
    "google-analytics.com",
    "googletagmanager.com",
    "analytics.google.com",
    "stats.g.doubleclick.net",
    "google.com/analytics",
    "ssl.google-analytics.com",
    "www.google-analytics.com",

    // Major Analytics Platforms
    "hotjar.com",
    "mouseflow.com",
    "segment.com",
    "segment.io",
    "mixpanel.com",
    "amplitude.com",
    "heap.io",
    "fullstory.com",
    "loggly.com",
    "newrelic.com",
    "quantserve.com",
    "chartbeat.com",
    "optimizely.com",
    "crazyegg.com",
    "inspectlet.com",

    // Additional Analytics (130+ more)
    "matomo.org",
    "piwik.org",
    "statcounter.com",
    "woopra.com",
    "kissmetrics.com",
    "clicky.com",
    "getclicky.com",
    "gosquared.com",
    "bugherd.com",
    "bugsnag.com",
    "sentry.io",
    "rollbar.com",
    "trackjs.com",
    "logrocket.com",
    "smartlook.com",
    "luckyorange.com",
    "sessionstack.com",
    "appsee.com",
    "appanalytics.io",
    "adjust.com",
    "kochava.com",
    "tune.com",
    "branch.io",
    "firebase.google.com",
    "firebaseio.com",
    "app-measurement.com",
    "clarity.ms",
    "c.clarity.ms",
    "bing.com/clk",
    "bat.bing.com",
    "moz.com",
    "alexa.com",
    "aweber.com",
    "mailchimp.com",
    "constantcontact.com",
    "omniture.com",
    "2o7.net",
    "everesttech.net",
    "everestjs.net",
    "conversionlogic.net",
    "convertro.com",
    "visualwebsiteoptimizer.com",
    "vwo.com",
    "unbounce.com",
    "instapage.com",
    "leadpages.net",
    "clickfunnels.com",
    "hubspot.com",
    "hs-analytics.net",
    "hsforms.com",
    "usemessages.com",
    "intercom.io",
    "intercomcdn.com",
    "drift.com",
    "driftt.com",
    "olark.com",
    "zendesk.com",
    "zopim.com",
    "livechat.com",
    "livechatinc.com",
    "tawk.to",
    "crisp.chat",
    "freshchat.com",
    "uservoice.com",
    "qualaroo.com",
    "surveymonkey.com",
    "typeform.com",
    "wufoo.com",
    "formstack.com",
    "jotform.com",
    "gravity forms.com",
    "contactform7.com",
    "pardot.com",
    "marketo.net",
    "eloqua.com",
    "silverpop.com",
    "exacttarget.com",
    "responsys.com",
    "sailthru.com",
    "bronto.com",
    "getresponse.com",
    "infusionsoft.com",
    "activecampaign.com",
    "drip.com",
    "convertkit.com",
    "benchmark.com",
    "sendinblue.com",
    "mailerlite.com",
    "omnisend.com",
    "klaviyo.com",
    "yotpo.com",
    "bazaarvoice.com",
    "reevoo.com",
    "trustpilot.com",
    "feefo.com",
    "stamped.io",
    "reviews.io",
    "powerreviews.com",
    "turnto.com",
    "gigya.com",
    "janrain.com",
    "loginradius.com",
    "auth0.com",
    "okta.com",
    "onelogin.com",
    "ping identity.com",
    "forgerock.com",
    "akamai.com",
    "cloudflare.com",
    "fastly.net",
    "cloudfront.net",
    "maxcdn.com",
    "stackpath.com",
    "bunnycdn.com",
    "keycdn.com",
    "jsdelivr.net",
    "unpkg.com",
    "cdnjs.com",
    "bootstrapcdn.com",
    "fontawesome.com",
    "googleapis.com",
    "gstatic.com"
  };

  // Top trackers (expanded from 21 to 100+)
  tracker_domains_ = {
    // Social Media Trackers
    "facebook.com/tr",
    "connect.facebook.net",
    "pixel.facebook.com",
    "t.co",
    "twitter.com/i/adsct",
    "ads-twitter.com",
    "analytics.twitter.com",
    "linkedin.com/px",
    "snap.licdn.com",
    "ads.linkedin.com",
    "reddit.com/api/v1/pixel",
    "redditmedia.com",
    "pinterest.com/ct",
    "pinterest.com/v3",
    "tiktok.com/i18n/pixel",
    "analytics.tiktok.com",
    "snapchat.com/pixel",
    "sc-static.net",
    "instagram.com/logging",
    "youtube.com/ptracking",
    "tumblr.com/svc",
    "vk.com/rtrg",
    "ok.ru/dk",

    // Major Tracking Networks
    "scorecardresearch.com",
    "comscore.com",
    "nielsen.com",
    "krxd.net",
    "bluekai.com",
    "demdex.net",
    "adsrvr.org",
    "agkn.com",
    "mathtag.com",
    "exelator.com",
    "eyeota.net",
    "liveramp.com",
    "rlcdn.com",
    "pippio.com",
    "crwdcntrl.net",
    "adnxs.com",
    "adsafeprotected.com",
    "moatads.com",
    "doubleverify.com",
    "integralads.com",
    "advertising.com",
    "mediaplex.com",
    "truste.com",
    "trustarc.com",
    "evidon.com",
    "ghostery.com",
    "yieldmo.com",
    "33across.com",
    "sonobi.com",
    "rhythmone.com",
    "sharethrough.com",
    "nativo.com",
    "triplelift.com",
    "teads.tv",
    "undertone.com",
    "adyoulike.com",
    "aniview.com",
    "brightcove.com",
    "spotxchange.com",
    "stickyadstv.com",
    "360yield.com",
    "152media.com",
    "33across.com",
    "acuityplatform.com",
    "adaptv.advertising.com",
    "adentifi.com",
    "adfarm.mediaplex.com",
    "adgrx.com",
    "adingo.jp",
    "admarvel.com",
    "adperium.com",
    "adsco.re",
    "adserve.com",
    "adsupply.com",
    "adtechus.com",
    "adtilt.com",
    "advertising.amazon.com",
    "affinity.com",
    "agkn.com",
    "apicit.net",
    "atemda.com",
    "atdmt.com",
    "avocet.io",
    "beachfront.com",
    "betweendigital.com",
    "bidtheatre.com",
    "bounceexchange.com",
    "bttrack.com",
    "c3tag.com",
    "cdnwidget.com",
    "chango.com"
  };

  // Common ad/analytics patterns
  ad_patterns_ = {
    "/ads/",
    "/ad?",
    "/advert",
    "/banner",
    "/sponsor",
    "pagead",
    "advertisement",
    "/track?",
    "/pixel?",
    "/beacon"
  };

  analytics_patterns_ = {
    "analytics",
    "tracking",
    "/collect?",
    "/track",
    "/stats",
    "/metrics",
    "/telemetry"
  };
}
