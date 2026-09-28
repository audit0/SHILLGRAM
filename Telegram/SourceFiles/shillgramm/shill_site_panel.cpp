/*
ShillGramm: shillvpn.site in the app's own web window.
*/
#include "shillgramm/shill_site_panel.h"

#include "base/weak_ptr.h"
#include "core/file_utilities.h"
#include "settings.h"
#include "shillgramm/shill_vpn.h"
#include "ui/chat/attach/attach_bot_downloads.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/text/text.h"
#include "webview/webview_common.h"
#include "webview/webview_interface.h"
#include "window/themes/window_theme.h"

namespace Shill {
namespace {

// The site is a plain web page, not a Mini App: everything a bot could ask
// of Telegram through the bridge is refused.
class SitePanel final
	: public Ui::BotWebView::Delegate
	, public base::has_weak_ptr {
public:
	void open(const QString &url) {
		_panel = nullptr;
		_panel = Ui::BotWebView::Show({
			.url = url,
			.storageId = storageId(),
			.title = rpl::single(u"SHILLVPN"_q),
			.bottom = rpl::single(u"shillvpn.site"_q),
			.delegate = static_cast<Ui::BotWebView::Delegate*>(this),
			.menuButtons = Ui::BotWebView::MenuButton::None,
		});
		_panel->requestActivate();
	}

	Webview::ThemeParams botThemeParams() override {
		return Window::Theme::WebViewParams();
	}
	Ui::Text::MarkedContext botTextContext() override {
		return {};
	}
	auto botDownloads(bool forceCheck)
	-> const std::vector<Ui::BotWebView::DownloadsEntry> & override {
		return _downloads;
	}
	void botDownloadsAction(
		uint32 id,
		Ui::BotWebView::DownloadsAction type) override {
	}
	bool botHandleLocalUri(QString uri, bool keepOpen) override {
		return false;
	}
	void botHandleInvoice(QString slug) override {
	}
	void botHandleMenuButton(Ui::BotWebView::MenuButton button) override {
	}
	bool botValidateExternalLink(QString uri) override {
		const auto lower = uri.toLower();
		return lower.startsWith(u"https://"_q)
			|| lower.startsWith(u"http://"_q);
	}
	void botOpenIvLink(QString uri) override {
		File::OpenUrl(uri);
	}
	void botSendData(QByteArray data) override {
	}
	void botSwitchInlineQuery(
		std::vector<QString> chatTypes,
		QString query) override {
	}
	void botCheckWriteAccess(Fn<void(bool allowed)> callback) override {
		callback(false);
	}
	void botAllowWriteAccess(Fn<void(bool allowed)> callback) override {
		callback(false);
	}
	bool botStorageWrite(
		QString key,
		std::optional<QString> value) override {
		return false;
	}
	std::optional<QString> botStorageRead(QString key) override {
		return std::nullopt;
	}
	void botStorageClear() override {
	}
	void botRequestEmojiStatusAccess(
		Fn<void(bool allowed)> callback) override {
		callback(false);
	}
	void botSharePhone(Fn<void(bool shared)> callback) override {
		callback(false);
	}
	void botInvokeCustomMethod(
		Ui::BotWebView::CustomMethodRequest request) override {
		request.callback(base::make_unexpected(u"UNSUPPORTED"_q));
	}
	void botSetEmojiStatus(
		Ui::BotWebView::SetEmojiStatusRequest request) override {
		request.callback(u"UNSUPPORTED"_q);
	}
	void botDownloadFile(
		Ui::BotWebView::DownloadFileRequest request) override {
		request.callback(false);
	}
	void botResolveButtonEmoji(
		Ui::BotWebView::ResolveButtonEmojiRequest request) override {
		request.callback(QImage());
	}
	void botSendPreparedMessage(
		Ui::BotWebView::SendPreparedMessageRequest request) override {
		request.callback(u"UNSUPPORTED"_q);
	}
	void botRequestChat(
		Ui::BotWebView::RequestChatRequest request) override {
		request.callback(u"UNSUPPORTED"_q);
	}
	void botVerifyAge(int age) override {
	}
	void botOpenPrivacyPolicy() override {
		File::OpenUrl(u"https://shillvpn.site/legal/privacy"_q);
	}
	void botClose() override {
		crl::on_main(this, [=] {
			_panel = nullptr;
			// Days bought or renewed show up in the subscription.
			if (Vpn::Instance().hasSubscription()) {
				Vpn::Instance().refresh();
			}
		});
	}

private:
	[[nodiscard]] Webview::StorageId storageId() const {
		auto token = KeychainRead(u"webview_token"_q).value_or(QByteArray());
		if (token.isEmpty()) {
			token = QByteArray::fromStdString(
				Webview::GenerateStorageToken());
			KeychainWrite(u"webview_token"_q, token);
		}
		return {
			.path = cWorkingDir() + u"tdata/webview-shillvpn"_q,
			.token = token,
		};
	}

	std::unique_ptr<Ui::BotWebView::Panel> _panel;
	std::vector<Ui::BotWebView::DownloadsEntry> _downloads;

};

} // namespace

void OpenSitePanel(const QString &url) {
	static auto panel = new SitePanel(); // Lives until exit.
	panel->open(url);
}

} // namespace Shill
