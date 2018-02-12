#ifndef TCPLAP_PARSERS_H
#define TCPLAP_PARSERS_H

// Customized TCLAP parsers.

// Supports all integer inputs given as hex.
#define TCLAP_SETBASE_ZERO 1
#include <tclap/CmdLine.h>

#include <cstddef>

namespace TCLAP {


/**
 * A Customized MultiArg parser which swallows all args to the next flag
 * arg. This allows users to specify shell wildcards, e.g.:

  program -i foo*

 *  parsing stops when on of the args starts with the flag separator.
 */
class MultiArgToNextFlag : public TCLAP::MultiArg<std::string> {
  public:
    MultiArgToNextFlag(
        const std::string& flag,
        const std::string& name,
        const std::string& desc,
        bool req,
        const std::string& typeDesc): TCLAP::MultiArg<std::string>(
                flag, name, desc, req, typeDesc) {}

    bool processArg(int* i, std::vector<std::string>& args) override;
};


class SizeArg : public TCLAP::ValueArg<size_t> {
 public:
    SizeArg(const std::string& flag,
        const std::string& name,
        const std::string& desc,
        bool req,
        size_t val,
        const std::string& typeDesc): TCLAP::ValueArg<size_t>(
            flag, name, desc, req, val, typeDesc) {}

     bool processArg(int* i, std::vector<std::string>& args) override;
     void _extractValue( const std::string& val);
};



}  // namespace TCLAP

#endif // TCPLAP_PARSERS_H
