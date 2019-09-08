#include <sstream>
#include <set>
#include "grammar.h"
#include "draconity.h"
#include "server.h"
#include "phrase.h"


/* Attach an error to the grammar.

   This is primarily a convenience method to streamline error construction.

 */
void Grammar::record_error(std::string type, std::string msg, int rc, std::string name) {
    std::unordered_map<std::string, std::string> error;
    std::stringstream full_msg_stream;
    full_msg_stream << msg << ". Return code: " << rc;
    error["type"] = type;
    error["msg"] = full_msg_stream.str();
    error["name"] = name;
    this->errors.push_back(std::move(error));
}
