/*
ShillGramm: Telegram for AI agents (Claude Code, Codex) on this computer.

The owner asked for it on 27.09.2026 («чтобы у агентов был доступ к тг
сессиям надо просто интеграцию добавить в один клик», then «продолжай
мост»). One click ("Подключить в один клик") starts a local MCP server
(http://127.0.0.1:<port>/mcp, a random bearer token) and registers it in
the Claude Code and Codex configs found on this computer. The agents then
work through the accounts signed in here with a narrow set of tools:
list chats, read history, resolve @usernames, send text. Private chats are
not readable until the user allows it, and the agents write only to Saved
Messages and to the chats the user marks in the chat's menu. Every call
is kept in a short log the user sees in the box; "Отключить" stops the
server, removes it from both configs and changes the token.

The token lives in the macOS Keychain; message texts are never logged.
*/
#pragma once

#include <QtCore/QPointer>

class History;
class QTcpServer;
class QTcpSocket;

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Shill {

class AgentBridge final {
public:
	struct LogEntry {
		TimeId when = 0;
		QString text;
	};

	[[nodiscard]] static AgentBridge &Instance();

	// Application start: brings the server up again if it was on.
	void start();
	void stop();

	[[nodiscard]] bool enabled() const;
	[[nodiscard]] rpl::producer<bool> enabledValue() const;
	[[nodiscard]] QString url() const;
	[[nodiscard]] bool readPrivate() const;
	void setReadPrivate(bool value);

	// Turns the server on and writes it into Claude Code and Codex.
	void connectAgents(Fn<void(QString report)> done);
	// Turns it off, removes it from both and changes the token.
	void disconnectAgents(Fn<void(QString report)> done);

	[[nodiscard]] bool canWrite(not_null<History*> history) const;
	void setCanWrite(not_null<History*> history, bool value);

	[[nodiscard]] const std::deque<LogEntry> &log() const;
	[[nodiscard]] rpl::producer<> logChanged() const;

private:
	AgentBridge();

	bool listen();
	void accept();
	void read(not_null<QTcpSocket*> socket);
	void handle(
		not_null<QTcpSocket*> socket,
		const QByteArray &method,
		const QByteArray &path,
		const QMap<QByteArray, QByteArray> &headers,
		const QByteArray &body);
	void call(
		const QString &tool,
		const QJsonObject &arguments,
		Fn<void(QJsonValue result, QString error)> done);
	void addLog(const QString &text);
	void load();
	void save() const;
	[[nodiscard]] QByteArray token();
	void resetToken();

	QPointer<QTcpServer> _server;
	int _port = 0;
	bool _readPrivate = false;
	rpl::variable<bool> _enabled = false;
	// Per account (user id): chats the agents may write to.
	base::flat_map<uint64, base::flat_set<PeerId>> _writable;
	QByteArray _token;
	std::deque<LogEntry> _log;
	rpl::event_stream<> _logChanges;

};

void AgentsBox(not_null<Ui::GenericBox*> box);
void ShowAgentsBox(not_null<Window::SessionController*> controller);

// Chat menu: "Агенты могут писать сюда".
[[nodiscard]] bool CanOfferAgentWrite(not_null<History*> history);
[[nodiscard]] QString AgentWriteMenuLabel(not_null<History*> history);
void ToggleAgentWrite(
	not_null<Window::SessionController*> controller,
	not_null<History*> history);

} // namespace Shill
