/*
ShillGramm: reply templates.

Ready texts kept on this computer (tdata/shillgramm_templates.json), put
into the message field of the open chat from the Cmd+K palette.
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Shill {

struct Template {
	QString title;
	QString text;
};

[[nodiscard]] std::vector<Template> Templates();
void SaveTemplates(const std::vector<Template> &list);

void ShowTemplatesBox(not_null<Window::SessionController*> controller);
void InsertTemplate(
	not_null<Window::SessionController*> controller,
	const QString &text);

} // namespace Shill
