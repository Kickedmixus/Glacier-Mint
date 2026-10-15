#include "app.h"

#ifdef HAVE_WEBKIT
const char *browser_import_script =
    "(function(){"
    "if(window.top!==window)return;"
    "function send(x){window.webkit.messageHandlers.glacier.postMessage(JSON.stringify(x));}"
    "function source(){var a=[];"
    "document.querySelectorAll('textarea').forEach(function(e){if(e.value)a.push(e.value);});"
    "document.querySelectorAll('.CodeMirror').forEach(function(e){if(e.CodeMirror)a.push(e.CodeMirror.getValue());});"
    "document.querySelectorAll('[contenteditable=true]').forEach(function(e){if(e.innerText)a.push(e.innerText);});"
    "document.querySelectorAll('pre').forEach(function(e){if(e.innerText)a.push(e.innerText);});"
    "return a.find(function(x){return x.indexOf('mainImage')>=0;});}"
    "function add(){if(document.getElementById('glacier-import'))return;var b=document.createElement('button');"
    "b.id='glacier-import';b.textContent='Import shader into Glacier Mint';b.onclick=function(){var c=source();"
    "if(!c){send({error:'Open the shader Image code editor first, then try again.'});return;}"
    "send({code:c,title:document.title,id:(location.pathname.match(/\\/view\\/([^/?]+)/)||[])[1]||''});};"
    "b.style='position:fixed;z-index:2147483647;right:18px;top:18px;padding:10px 14px;background:#a9e0ef;color:#071014;border:1px solid #071014;font:600 14px sans-serif;cursor:pointer';"
    "document.body.appendChild(b);}"
    "add();new MutationObserver(add).observe(document.documentElement,{childList:true,subtree:true});"
    "})();";

void browser_message(WebKitUserContentManager *manager, WebKitJavascriptResult *result, App *app) {
    (void)manager; JSCValue *value = webkit_javascript_result_get_js_value(result); char *message = jsc_value_to_string(value); JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, message, -1, NULL)) { JsonObject *obj = json_node_get_object(json_parser_get_root(parser)); JsonNode *error = json_object_get_member(obj, "error"); JsonNode *code = json_object_get_member(obj, "code");
        if (error && JSON_NODE_HOLDS_VALUE(error)) set_status(app, json_node_get_string(error)); else if (code && JSON_NODE_HOLDS_VALUE(code)) { const char *title = "Imported Shadertoy shader"; const char *id = ""; JsonNode *title_node = json_object_get_member(obj, "title"), *id_node = json_object_get_member(obj, "id"); if (title_node && JSON_NODE_HOLDS_VALUE(title_node)) title = json_node_get_string(title_node); if (id_node && JSON_NODE_HOLDS_VALUE(id_node)) id = json_node_get_string(id_node); import_shader(app, json_node_get_string(code), title, id); gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0); }
    }
    g_object_unref(parser); g_free(message); webkit_javascript_result_unref(result);
}

static void browser_go(App *app) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(app->browser_url)); if (!text[0]) return; char *url = g_strdup(text);
    if (!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://")) { char *with_scheme = g_strdup_printf("https://%s", url); g_free(url); url = with_scheme; }
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app->browser), url); g_free(url);
}

void browser_back(GtkButton *button, App *app) { (void)button; if (webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(app->browser))) webkit_web_view_go_back(WEBKIT_WEB_VIEW(app->browser)); }
void browser_forward(GtkButton *button, App *app) { (void)button; if (webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(app->browser))) webkit_web_view_go_forward(WEBKIT_WEB_VIEW(app->browser)); }
void browser_load(GtkButton *button, App *app) { (void)button; browser_go(app); }
void browser_load_changed(WebKitWebView *view, WebKitLoadEvent event, App *app) { if (event == WEBKIT_LOAD_COMMITTED) { const char *uri = webkit_web_view_get_uri(view); if (uri) gtk_entry_set_text(GTK_ENTRY(app->browser_url), uri); } }
#endif

void browser_external_clicked(GtkButton *button, App *app) { (void)button; gtk_show_uri_on_window(GTK_WINDOW(app->window), "https://www.shadertoy.com", GDK_CURRENT_TIME, NULL); }
