#include "aff4/tclap_parsers.h"


bool TCLAP::MultiArgToNextFlag::processArg(int* i, std::vector<std::string>& args) {
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


bool TCLAP::SizeArg::processArg(int* i, std::vector<std::string>& args) {
    if ( _ignoreable && Arg::ignoreRest() )
        return false;

    if ( _hasBlanks( args[*i] ) )
        return false;

    std::string flag = args[*i];

    std::string value = "";
    trimFlag( flag, value );

    if ( argMatches( flag ) )
        {
            if ( _alreadySet )
                {
                    if ( _xorSet )
                        throw( CmdLineParseException(
                                   "Mutually exclusive argument already set!",
                                   toString()) );
                    else
                        throw( CmdLineParseException("Argument already set!",
                                                     toString()) );
                }

            if ( Arg::delimiter() != ' ' && value == "" )
                throw( ArgParseException(
                           "Couldn't find delimiter for this argument!",
                           toString() ) );

            if ( value == "" )
                {
                    (*i)++;
                    if ( static_cast<unsigned int>(*i) < args.size() )
                        _extractValue( args[*i] );
                    else
                        throw( ArgParseException("Missing a value for this argument!",
                                                 toString() ) );
                }
            else
                _extractValue( value );

            _alreadySet = true;
            _checkWithVisitor();
            return true;
        }
    else
        return false;
}


void TCLAP::SizeArg::_extractValue( const std::string& val )
{
    int last_digit = val.find_last_of("0123456789") + 1;
    std::string digits = val.substr(0, last_digit);
    std::string suffix = val.substr(last_digit);
    size_t numeric_value = 0;

    if (digits.size() == 0) {
       throw (TCLAP::CmdLineParseException( "Invalid number ",
                                                 toString()) );
    }

    try {
        ExtractValue(numeric_value, digits,
                     typename TCLAP::ArgTraits<size_t>::ValueCategory());

        if (suffix == "M" || suffix == "m") {
            numeric_value *= 1024*1024;
        } else if (suffix == "k" || suffix == "K") {
            numeric_value *= 1024;
        } else if (suffix == "g" || suffix == "G") {
            numeric_value *= 1024 * 1024 * 1024;
        } else if (suffix.size() > 0) {
            throw (TCLAP::CmdLineParseException( "Invalid suffix",
                                                 toString()) );
        }

        this->_value = numeric_value;

    } catch( ArgParseException &e) {
        throw ArgParseException(e.error(), toString());
    }
}
