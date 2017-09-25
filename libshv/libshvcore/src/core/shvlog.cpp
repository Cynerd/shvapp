#include "shvlog.h"
#include "string.h"

#include <map>
#include <cctype>
//#include <algorithm>
#include <iostream>

#ifdef __unix
#include <unistd.h>
#include <cstdio>
#endif

namespace shv {
namespace core {

//#define SHVLOG_DEBUG
#ifdef SHVLOG_DEBUG
#define mylog std::cerr
#else
#define mylog if(false) std::cerr
#endif

namespace {

struct LogFilter
{
	ShvLog::Level defaultModulesLogTreshold = ShvLog::Level::Info;
	std::map<std::string, ShvLog::Level> modulesTresholds;
	std::map<std::string, ShvLog::Level> categoriesTresholds;
	ShvLog::Level defaultCategoriesLogTreshold = ShvLog::Level::Invalid;
} s_globalLogFilter;

bool s_logLongFileNames = false;

std::string moduleFromFileName(const char *file_name)
{
	if(s_logLongFileNames)
		return std::string(file_name);
	std::string ret(file_name);
	auto ix = ret.find_last_of('/');
#ifndef __unix
	if(ix == std::string::npos)
		ix = ret.find_last_of('\\');
#endif
	if(ix != std::string::npos)
		ret = ret.substr(ix + 1);
	return ret;
}

/*
Here, \033 is the ESC character, ASCII 27.
It is followed by [,
then zero or more numbers separated by ;,
and finally the letter m.

		 foreground background
black        30         40
red          31         41
green        32         42
yellow       33         43
blue         34         44
magenta      35         45
cyan         36         46
white        37         47

reset             0  (everything back to normal)
bold/bright       1  (often a brighter shade of the same colour)
underline         4
inverse           7  (swap foreground and background colours)
bold/bright off  21
underline off    24
inverse off      27
 */
enum TTYColor {Black=0, Red, Green, Yellow, Blue, Magenta, Cyan, White};

std::ostream & setTtyColor(std::ostream &out, TTYColor color, bool bright, bool bg_color)
{
	out << "\033[" << (bright? '1': '0') << ';' << (bg_color? '4': '3') << char('0' + color) << 'm';
	return out;
}

void default_message_output(ShvLog::Level level, const ShvLog::LogContext &context, const std::string &message)
{
	static int n = 0;
	bool is_tty = false;
#ifdef __unix
	is_tty = ::isatty(STDERR_FILENO);
#endif
	auto set_tty_color = [is_tty](TTYColor color, bool bright = false, bool bg_color = false) -> std::ostream & {
		if(is_tty)
			return setTtyColor(std::cerr, color, bright, bg_color);
		return std::cerr;
	};
	set_tty_color(TTYColor::Green, true) << ++n;
	TTYColor log_color;
	bool stay_bright = false;
	switch(level) {
	case ShvLog::Level::Fatal:
		stay_bright = true; log_color = TTYColor::Red; set_tty_color(log_color, true) << "|F|";
		break;
	case ShvLog::Level::Error:
		stay_bright = true; log_color = TTYColor::Red; set_tty_color(log_color, true) << "|E|";
		break;
	case ShvLog::Level::Warning:
		stay_bright = true; log_color = TTYColor::Magenta; set_tty_color(log_color, true) << "|W|";
		break;
	case ShvLog::Level::Info:
		log_color = TTYColor::Cyan; set_tty_color(log_color, true) << "|I|";
		break;
	case ShvLog::Level::Debug:
		log_color = TTYColor::White; set_tty_color(log_color, true) << "|D|";
		break;
	default:
		log_color = TTYColor::Yellow; set_tty_color(log_color, true) << "|?|";
		break;
	};
	if(context.category && context.category[0])
		set_tty_color(TTYColor::Yellow, true) << '(' << context.category << ')';
	set_tty_color(TTYColor::White, true) << '[' << moduleFromFileName(context.file) << ':' << context.line << "] ";
	set_tty_color(log_color, stay_bright) << message;
	if(is_tty)
		std::cerr << "\33[0m";
	std::cerr << std::endl;
}

ShvLog::MessageOutput message_output = default_message_output;

std::pair<std::string, ShvLog::Level> parseCategoryLevel(const std::string &category)
{
	auto ix = category.find(':');
	ShvLog::Level level = ShvLog::Level::Debug;
	std::string cat = category;
	if(ix != std::string::npos) {
		std::string s = category.substr(ix + 1, 1);
		char l = s.empty()? '\0': std::toupper(s[0]);
		cat = category.substr(0, ix);
		String::trim(cat);
		if(l == 'D')
			level = ShvLog::Level::Debug;
		else if(l == 'I')
			level = ShvLog::Level::Info;
		else if(l == 'W')
			level = ShvLog::Level::Warning;
		else if(l == 'E')
			level = ShvLog::Level::Error;
		else
			level = ShvLog::Level::Invalid;
	}
	return std::pair<std::string, ShvLog::Level>(cat, level);
}

void setModulesTresholds(const std::vector<std::string> &tresholds)
{
	if(tresholds.empty()) {
		mylog << "setting defaultModulesLogTreshold to: " << ShvLog::levelToString(ShvLog::Level::Debug) << std::endl;
		s_globalLogFilter.defaultModulesLogTreshold = ShvLog::Level::Debug;
	}
	else for(std::string module : tresholds) {
		std::pair<std::string, ShvLog::Level> lev = parseCategoryLevel(module);
		if(lev.first.empty()) {
			mylog << "setting defaultModulesLogTreshold to: " << ShvLog::levelToString(lev.second) << std::endl;
			s_globalLogFilter.defaultModulesLogTreshold = lev.second;
		}
		else {
			mylog << "setting moduleTreshold: " << lev.first << " to: " << ShvLog::levelToString(lev.second) << std::endl;
			s_globalLogFilter.modulesTresholds[lev.first] = lev.second;
		}
	}
}

std::vector<std::string> setModulesTresholdsFromArgs(const std::vector<std::string> &args)
{
	std::vector<std::string> ret;
	s_globalLogFilter.modulesTresholds.clear();
	for(size_t i=0; i<args.size(); i++) {
		const std::string &s = args[i];
		if(s == "-d" || s == "--debug") {
			i++;
			std::string tresholds =  (i < args.size())? args[i]: std::string();
			if(!tresholds.empty() && tresholds[0] == '-') {
				i--;
				continue;
			}
			setModulesTresholds(String::split(tresholds, ','));
		}
		else {
			ret.push_back(s);
		}
	}
	return ret;
}

void setCategoriesTresholds(const std::vector<std::string> &tresholds)
{
	if(tresholds.empty()) {
		mylog << "setting defaultCategoriesLogTreshold to: " << ShvLog::levelToString(ShvLog::Level::Debug) << std::endl;
		s_globalLogFilter.defaultCategoriesLogTreshold = ShvLog::Level::Debug;
	}
	else for(std::string category : tresholds) {
		std::pair<std::string, ShvLog::Level> lev = parseCategoryLevel(category);
		if(lev.first.empty()) {
			mylog << "setting defaultCategoriesLogTreshold to: " << ShvLog::levelToString(lev.second) << std::endl;
			s_globalLogFilter.defaultCategoriesLogTreshold = lev.second;
		}
		else {
			mylog << "setting categorieTreshold: " << lev.first << " to: " << ShvLog::levelToString(lev.second) << std::endl;
			s_globalLogFilter.categoriesTresholds[lev.first] = lev.second;
		}
	}
}

std::vector<std::string> setCategoriesTresholdsFromArgs(const std::vector<std::string> &args)
{
	using namespace std;
	vector<string> ret;
	s_globalLogFilter.categoriesTresholds.clear();
	for(size_t i=0; i<args.size(); i++) {
		const string &s = args[i];
		if(s == "-v" || s == "--verbose") {
			i++;
			std::string tresholds =  (i < args.size())? args[i]: std::string();
			if(!tresholds.empty() && tresholds[0] == '-') {
				i--;
				continue;
			}
			setCategoriesTresholds(String::split(tresholds, ','));
		}
		else {
			ret.push_back(s);
		}
	}
	return ret;
}

}

ShvLog::~ShvLog()
{
	if (!--stream->ref) {
		if(stream->level > Level::Invalid) {
			std::streambuf *buff = stream->ts.rdbuf();
			std::stringbuf *sbuff = dynamic_cast<std::stringbuf*>(buff);
			if(sbuff) {
				std::string msg = sbuff->str();
				if (stream->space && !msg.empty() && msg.back() == ' ')
					msg.pop_back();
				message_output(stream->level, stream->context, msg);
			}
		}
		delete stream;
	}
}

std::vector<std::string> ShvLog::setGlobalTresholds(int argc, char *argv[])
{
	std::vector<std::string> ret;
	for(int i=1; i<argc; i++) {
		const std::string &s = argv[i];
		if(s == "-lfn" || s == "--log-long-file-names") {
			i++;
			s_logLongFileNames = true;
		}
		else {
			ret.push_back(s);
		}
	}
	ret = setModulesTresholdsFromArgs(ret);
	ret = setCategoriesTresholdsFromArgs(ret);
	ret.insert(ret.begin(), argv[0]);
	return ret;
}

void ShvLog::setMessageOutput(ShvLog::MessageOutput out)
{
	message_output = out;
}

ShvLog::MessageOutput ShvLog::messageOutput()
{
	return message_output;
}

std::string ShvLog::modulesLogInfo()
{
	std::string ret;
	for (auto& kv : s_globalLogFilter.modulesTresholds) {
		if(!ret.empty())
			ret += ',';
		ret += kv.first + ':' + levelToString(kv.second)[0];
	}
	if(!ret.empty())
		ret += ',';
	ret = ret + ':' + levelToString(s_globalLogFilter.defaultModulesLogTreshold)[0];
	return ret;
}

std::string ShvLog::categoriesLogInfo()
{
	std::string ret;
	for (auto& kv : s_globalLogFilter.categoriesTresholds) {
		if(!ret.empty())
			ret += ',';
		ret += kv.first + ':' + levelToString(kv.second)[0];
	}
	if(s_globalLogFilter.defaultCategoriesLogTreshold != Level::Invalid) {
		if(!ret.empty())
			ret += ',';
		ret = ret + ':' + levelToString(s_globalLogFilter.defaultCategoriesLogTreshold)[0];
	}
	return ret;
}

bool ShvLog::isMatchingLogFilter(ShvLog::Level level, const ShvLog::LogContext &log_context)
{
	const LogFilter &log_filter = s_globalLogFilter;
	if(level == ShvLog::Level::Fatal) {
		return true;
	}
	if(log_context.category && log_context.category[0]) {
		// if category is specified, than module logging rules are ignored
		std::string cat(log_context.category);
#ifdef EXACT_CATEGORY_MATCH
		auto it = log_filter.categoriesTresholds.find(cat);
		ShvLog::Level category_level = (it == log_filter.categoriesTresholds.end())? log_filter.defaultCategoriesLogTreshold: it->second;
		return (level <= category_level);
#else
		for (auto& kv : log_filter.categoriesTresholds) {
			//mylog << cat << " vs " << kv.first << " == " << (String::indexOf(cat, kv.first, String::CaseInsensitive) != std::string::npos) << std::endl;
			if(String::indexOf(cat, kv.first, String::CaseInsensitive) != std::string::npos)
				return level <= kv.second;
		}
		if(log_filter.defaultCategoriesLogTreshold != Level::Invalid)
			return level <= log_filter.defaultCategoriesLogTreshold;
#endif
		return false;
	}
	{
		std::string module = moduleFromFileName(log_context.file);
		for (auto& kv : log_filter.modulesTresholds) {
			if(String::indexOf(module, kv.first, String::CaseInsensitive) != std::string::npos)
				return level <= kv.second;
		}
	}
	return level <= log_filter.defaultModulesLogTreshold;
}

const char* ShvLog::levelToString(ShvLog::Level level)
{
	switch(level) {
	case ShvLog::Level::Fatal:
		return "Fatal";
	case ShvLog::Level::Error:
		return "Error";
	case ShvLog::Level::Warning:
		return "Warning";
	case ShvLog::Level::Info:
		return "Info";
	case ShvLog::Level::Debug:
		return "Debug";
	case ShvLog::Level::Invalid:
		return "Invalid";
	}
	return "???";
}

const char *ShvLog::logCLIHelp()
{
	return
		"-lfn, --log-long-file-names\n"
		"\tLog long file names\n"
		"-d, --log-file [<pattern>]:[D|I|W|E]\n"
		"\tSet file log treshold\n"
		"\tset treshold for all files containing pattern to treshold\n"
		"\twhen pattern is not set, set treshold for all files\n"
		"\twhen treshold is not set, set treshold D (Debug) for all files containing pattern\n"
		"\twhen nothing is not set, set treshold D (Debug) for all files\n"
		"\tExamples:\n"
		"\t\t-d\t\tset treshold D (Debug) for all files\n"
		"\t\t-d :W\t\tset treshold W (Warning) for all files\n"
		"\t\t-d foo,bar\t\tset treshold D for all files containing 'foo' or 'bar'\n"
		"\t\t-d bar:W\tset treshold W (Warning) for all files containing 'bar'\n"
		"-v, --log-category [<pattern>]:[D|I|W|E]\n"
		"\tSet category log treshold\n"
		"\tset treshold for all categories containing pattern to treshold\n"
		"\tthe same rules as for module logging are applied to categiries\n";
}

}}
