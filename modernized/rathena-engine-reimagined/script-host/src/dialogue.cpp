#include "script_host/dialogue.hpp"

#include "script_host/not_implemented.hpp"

namespace script_host {

DialogueState DialogueManager::start(PlayerId, const std::string&, const std::string&) { not_implemented("DialogueManager::start"); }
DialogueState DialogueManager::resume(PlayerId, const ResumeToken&, const DialogueResponse&) { not_implemented("DialogueManager::resume"); }
void DialogueManager::on_disconnect(PlayerId) { not_implemented("DialogueManager::on_disconnect"); }
void DialogueManager::advance_time(std::uint64_t) { not_implemented("DialogueManager::advance_time"); }
bool DialogueManager::has_dialogue(PlayerId) const { not_implemented("DialogueManager::has_dialogue"); }

}  // namespace script_host
