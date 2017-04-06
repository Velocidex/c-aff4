#ifndef TCPLAP_PARSERS_H
#define TCPLAP_PARSERS_H

// Customized TCLAP parsers.

// Supports all integer inputs given as hex.
#define TCLAP_SETBASE_ZERO 1
#include <tclap/CmdLine.h>

namespace TCLAP {


/**
 * A Customized MultiArg parser which swallows all args to the next flag
 * arg. This allows users to specify shell wildcards, e.g.:

  program -i foo*

 *  parsing stops when on of the args starts with the flag separator.
 */
template<class T>
class MultiArgToNextFlag : public TCLAP::MultiArg<T> {
  public:
    MultiArgToNextFlag(
        const std::string& flag,
        const std::string& name,
        const std::string& desc,
        bool req,
        const std::string& typeDesc): TCLAP::MultiArg<T>(
                flag, name, desc, req, typeDesc) {};

    virtual bool processArg(int* i, std::vector<std::string>& args);
};

template<class T>
bool MultiArgToNextFlag<T>::processArg(int* i, std::vector<std::string>& args) {
    if ( this->_ignoreable && Arg::ignoreRest() ) {
        return false;
    }

    if ( this->_hasBlanks( args[*i] ) ) {
        return false;
    }

    std::string flag = args[*i];
    std::string value = "";

    this->trimFlag( flag, value );

    if ( this->argMatches( flag ) ) {
        if ( Arg::delimiter() != ' ' && value == "" )
            throw( ArgParseException(
                       "Couldn't find delimiter for this argument!",
                       this->toString() ) );

        // always take the first one, regardless of start string
        if ( value == "" ) {
            (*i)++;
            if ( static_cast<unsigned int>(*i) < args.size() ) {
                this->_extractValue( args[*i] );
            } else
                throw( ArgParseException("Missing a value for this argument!",
                                         this->toString() ) );
        } else {
            this->_extractValue( value );
        }

        // continuing taking the args until we hit one with a start string
        while ( (unsigned int)(*i)+1 < args.size()) {
            std::string arg = args[(*i)+1];

            // Stop if the arg starts with a flag character.
            if ((arg.compare(0, Arg::flagStartString().size(),
                             Arg::flagStartString()) == 0) ||
                    (arg.compare(0, Arg::nameStartString().size(),
                                 Arg::nameStartString()) == 0)) {
                break;
            };

            this->_extractValue(arg);
            (*i)++;
        };

        this->_alreadySet = true;
        this->_checkWithVisitor();

        return true;
    } else {
        return false;
    }
}


}

#endif // TCPLAP_PARSERS_H
