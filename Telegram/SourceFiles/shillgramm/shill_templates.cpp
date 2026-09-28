/*
ShillGramm: reply templates.
*/
#include "shillgramm/shill_templates.h"

#include "history/history_widget.h"
#include "mainwidget.h"
#include "settings.h"
#include "shillgramm/shill_snooze.h" // Tr
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Shill {
namespace {

constexpr auto kMaxTemplates = 100;
constexpr auto kMaxTitle = 64;
constexpr auto kMaxText = 4096;

[[nodiscard]] QString TemplatesPath() {
	return cWorkingDir() + u"tdata/shillgramm_templates.json"_q;
}

void EditTemplateBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		int index) {
	auto list = Templates();
	const auto existing = (index >= 0 && index < int(list.size()));
	const auto current = existing ? list[index] : Template();
	box->setTitle(rpl::single(existing
		? Tr("Template", "Шаблон")
		: Tr("New template", "Новый шаблон")));
	box->setWidth(st::boxWideWidth);

	const auto title = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::NoNewlines,
		rpl::single(Tr("Name, e.g. «Price»", "Название, например «Цены»")),
		current.title));
	title->setMaxLength(kMaxTitle);
	box->addSkip(st::boxLittleSkip);
	const auto text = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::newGroupDescription,
		Ui::InputField::Mode::MultiLine,
		rpl::single(Tr("Text of the reply", "Текст ответа")),
		current.text));
	text->setMaxLength(kMaxText);
	box->setFocusCallback([=] {
		(existing ? text : title)->setFocusFast();
	});

	box->addButton(rpl::single(Tr("Save", "Сохранить")), [=] {
		const auto name = title->getLastText().trimmed();
		const auto body = text->getLastText().trimmed();
		if (name.isEmpty()) {
			title->showError();
			return;
		} else if (body.isEmpty()) {
			text->showError();
			return;
		}
		auto list = Templates();
		if (existing && index < int(list.size())) {
			list[index] = { name, body };
		} else if (int(list.size()) < kMaxTemplates) {
			list.push_back({ name, body });
		}
		SaveTemplates(list);
		box->closeBox();
		ShowTemplatesBox(controller);
	});
	if (existing) {
		box->addLeftButton(rpl::single(Tr("Delete", "Удалить")), [=] {
			auto list = Templates();
			if (index < int(list.size())) {
				list.erase(begin(list) + index);
				SaveTemplates(list);
			}
			box->closeBox();
			ShowTemplatesBox(controller);
		});
	}
	box->addButton(rpl::single(Tr("Cancel", "Отмена")), [=] {
		box->closeBox();
	});
}

void TemplatesBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(rpl::single(Tr("Reply templates", "Шаблоны ответов")));
	box->setWidth(st::boxWideWidth);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(Tr(
			"Kept on this computer. Press Cmd+K in a chat and type a "
			"template's name to put it into the message field.",
			"Хранятся на этом компьютере. В чате нажмите Cmd+K и начните "
			"вводить название шаблона — он встанет в поле сообщения.")),
		st::boxDividerLabel));
	box->addSkip(st::boxLittleSkip);

	const auto list = Templates();
	for (auto i = 0; i != int(list.size()); ++i) {
		const auto button = box->addRow(
			object_ptr<Ui::SettingsButton>(
				box,
				rpl::single(list[i].title),
				st::settingsButtonNoIcon),
			style::margins());
		button->setClickedCallback([=] {
			box->closeBox();
			controller->show(Box(EditTemplateBox, controller, i));
		});
	}
	const auto add = box->addRow(
		object_ptr<Ui::SettingsButton>(
			box,
			rpl::single(Tr("Add a template", "Добавить шаблон")),
			st::settingsButtonNoIcon),
		style::margins());
	add->setClickedCallback([=] {
		box->closeBox();
		controller->show(Box(EditTemplateBox, controller, -1));
	});
	box->addButton(rpl::single(Tr("Close", "Закрыть")), [=] {
		box->closeBox();
	});
}

} // namespace

std::vector<Template> Templates() {
	auto file = QFile(TemplatesPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	auto result = std::vector<Template>();
	const auto array = QJsonDocument::fromJson(file.readAll()).array();
	for (const auto &value : array) {
		const auto object = value.toObject();
		const auto title = object.value(u"title"_q).toString().left(kMaxTitle);
		const auto text = object.value(u"text"_q).toString().left(kMaxText);
		if (!title.isEmpty() && !text.isEmpty()) {
			result.push_back({ title, text });
		}
		if (int(result.size()) >= kMaxTemplates) {
			break;
		}
	}
	return result;
}

void SaveTemplates(const std::vector<Template> &list) {
	auto array = QJsonArray();
	for (const auto &entry : list) {
		array.append(QJsonObject{
			{ u"title"_q, entry.title },
			{ u"text"_q, entry.text },
		});
	}
	auto file = QFile(TemplatesPath());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
		file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
	}
}

void ShowTemplatesBox(not_null<Window::SessionController*> controller) {
	controller->show(Box(TemplatesBox, controller));
}

void InsertTemplate(
		not_null<Window::SessionController*> controller,
		const QString &text) {
	if (const auto history = controller->content()->shillHistoryWidget()) {
		history->insertTextAtCursor(text);
		history->setInnerFocus();
	}
}

} // namespace Shill
