#include "ticker_plant.hpp"
#include "market_logger.hpp"
#include "log_reporter.hpp"
#include "mtgox.hpp"
#include "enum_utils.hpp"

#include <boost/program_options.hpp>
#include <glog/logging.h>
#include <json/value.h>
#include <json/reader.h>

#include <exception>
#include <iostream>
#include <iomanip>
#include <memory>
#include <cstdlib>
#include <functional>
#include <string>
#include <sstream>


using namespace std;
using namespace btc_arb;

enum class SourceType {
  LDB, FLAT, WS_MTGOX, LDB_MTGOX
};

template<> char const* utils::EnumStrings<SourceType>::names[] = {
  "ldb", "flat", "ws_mtgox", "ldb_mtgox"};

enum class SinkType {
  FLAT, RAW_LDB
};

template<> char const* utils::EnumStrings<SinkType>::names[] = {
  "flat", "raw_ldb"};

SourceType str_to_source_type(const string& s) {
  if (s == "ldb") {
    return SourceType::LDB;
  } else if (s == "flat") {
    return SourceType::FLAT;
  } else if (s == "ws_mtgox") {
    return SourceType::WS_MTGOX;
  } else if (s == "ldb_mtgox") {
    return SourceType::LDB_MTGOX;
  } else {
    throw boost::program_options::invalid_option_value(s);
  }
}

struct SourcePath {
  static SourcePath parse(const string& spath);

  const SourceType type;
  const string path;
};

SourcePath SourcePath::parse(const string& spath) {
  string::size_type delim{spath.find(':')};
  if (delim == string::npos) {
    throw runtime_error("invalid path \'" + spath + "\'");
  }
  stringstream type_str{spath.substr(0, delim)};
  SourceType type;
  type_str >> utils::enum_from_str(type);
  string path{spath.substr(delim + 1)};
  return SourcePath{move(type), move(path)};
}

int main(int argc, char **argv) {
  namespace po = boost::program_options;
  namespace ph = std::placeholders;
  google::InitGoogleLogging(argv[0]);
  google::LogToStderr();

  string source_str{"ws_mtgox:ws://websocket.mtgox.com/mtgox"};

  stringstream desc_msg;
  desc_msg << "Ticker Plant -- persists market data and runs strategies "
           << "(live or backtest) " << endl
           << "Built on " << __TIMESTAMP__ << endl << endl
           << "usage: " << argv[0] << " [CONFIG] <SOURCE>" << endl << endl
           << "Allowed options:";

  auto description = po::options_description{desc_msg.str()};
  description.add_options()
      ("help,h", "prints this help message")
      ("source",
       po::value<string>(&source_str)->value_name("PPATH"),
       ("the market data souce; can also be specified as a positional arg; "
        "default=" + source_str).c_str())
      ("sink",
       po::value<vector<string>>()->value_name("S"),
       "specifies the source type (available: ldb, flat, ws_mtgox)");
  po::positional_options_description positional;
  positional.add("source", -1);

  auto variables = po::variables_map{};
  try {
    po::store(po::command_line_parser(argc, argv)
              .options(description).positional(positional).run(), variables);
    po::notify(variables);
    if (variables.count("help")) {
      cerr << description << endl;
      return 0;
    }

    if (variables.count("sink")) {
      cout << "sinks: ";
      for (auto& s : variables["sink"].as<vector<string>>()) {
        cout << s << " ";
      }
      cout << endl;
    }

    SourcePath spath = SourcePath::parse(source_str);
    unique_ptr<TickerPlant> plant{nullptr};
    switch (spath.type) {
      case SourceType::LDB:
        plant.reset(new LdbTickerPlant<FlatParser>(spath.path));
        break;
      case SourceType::FLAT:
        plant.reset(new FlatFileTickerPlant(spath.path));
        break;
      case SourceType::WS_MTGOX:
        plant.reset(new WebSocketTickerPlant<mtgox::FeedParser>(spath.path));
        break;
      case SourceType::LDB_MTGOX:
        plant.reset(new LdbTickerPlant<mtgox::FeedParser>(spath.path));
        break;
    }
    // LevelDBLogger market_log("market.leveldb");
    // LevelDBLogger trades_log("trades.leveldb", only_trades);
    FileLogger mlog("out.flat");
    cout << static_cast<int>(spath.type) << " " << spath.path << endl;

    // unique_ptr<TickerPlant> plant{new WebSocketTickerPlant(uri)};
    // plant->add_tick_handler(bind(&LevelDBLogger::log_tick, &market_log, ph::_1));
    // plant->add_tick_handler(bind(&LevelDBLogger::log_tick, &trades_log, ph::_1));
    // plant->add_tick_handler(bind(&FlatFileLogger::log_tick, &mlog, ph::_1));
    plant->add_tick_handler(report_progress_block);
    plant->add_tick_handler([&mlog](const Tick& tick) {
        // if (tick.type == Tick::Type::QUOTE) {
        //   const Quote& quote = tick.as<Quote>();
        //   LOG(INFO) << "Q " << quote.received << " " << quote.ex_time << " lag="
        //             << (quote.received - quote.ex_time)
        //             << " " << quote.total_volume << " @ " << quote.price;

        // } else if (tick.type == Tick::Type::TRADE) {
        //   const Trade& trade = tick.as<Trade>();
        // }
        // cout << static_cast<int>(tick.type) << endl;
        // mlog.log(reinterpret_cast<const char *>(&tick), sizeof(Tick));
      });
    LOG (INFO) << "starting ticker plant";
    // plant->run();
  } catch (const boost::program_options::unknown_option& e) {
    LOG(ERROR) << e.what();
    return 1;
  } catch (const boost::program_options::invalid_option_value& e) {
    LOG(ERROR) << e.what();
    return 2;
  } catch(const std::exception& e) {
    LOG(ERROR) << e.what();
    return -1;
  }
}
