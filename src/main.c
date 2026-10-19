#include "app.h"

#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "--restore")) return restore_wallpaper();
    if (argc >= 3 && !strcmp(argv[1], "--wallpaper")) {
        int fps = 30, speed = 100; gboolean vsync = TRUE, chroma = FALSE; float key[3] = { 0.0f, 1.0f, 0.0f }, smoothing = 0.06f;
        for (int i = 3; i + 1 < argc; i++) { if (!strcmp(argv[i], "--fps")) fps = atoi(argv[i + 1]); else if (!strcmp(argv[i], "--vsync")) vsync = atoi(argv[i + 1]) != 0; else if (!strcmp(argv[i], "--speed")) speed = atoi(argv[i + 1]); else if (!strcmp(argv[i], "--chroma")) chroma = atoi(argv[i + 1]) != 0; else if (!strcmp(argv[i], "--key-r")) key[0] = (float)g_ascii_strtod(argv[i + 1], NULL); else if (!strcmp(argv[i], "--key-g")) key[1] = (float)g_ascii_strtod(argv[i + 1], NULL); else if (!strcmp(argv[i], "--key-b")) key[2] = (float)g_ascii_strtod(argv[i + 1], NULL); else if (!strcmp(argv[i], "--smooth")) smoothing = (float)g_ascii_strtod(argv[i + 1], NULL); }
        return wallpaper_main(argv[2], CLAMP(fps, 1, 240), vsync, CLAMP(speed, 0, 200), chroma, key, CLAMP(smoothing, 0.005f, 0.30f));
    }

    gtk_init(&argc, &argv);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    add_css();

    App app = {0};
    app.started = g_get_monotonic_time();
    char *logo = logo_path();
    gtk_window_set_default_icon_from_file(logo, NULL);
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_icon_from_file(GTK_WINDOW(app.window), logo, NULL);
    g_free(logo);
    gtk_window_set_title(GTK_WINDOW(app.window), APP_NAME);
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1120, 960);
    gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_container_add(GTK_CONTAINER(app.window), build_ui(&app));
    populate_local(&app);
    g_timeout_add(16, (GSourceFunc)tick_preview, &app);
    gtk_widget_show_all(app.window);
    g_idle_add((GSourceFunc)restore_last_shader, &app);
    gtk_main();

    if (app.program) glDeleteProgram(app.program);
    g_free(app.shader_code); g_free(app.shader_title); g_free(app.shader_id); g_free(app.shader_path); g_free(app.constants_source); if (app.constants) g_ptr_array_free(app.constants, TRUE);
    if (app.wallpaper_widgets) g_ptr_array_free(app.wallpaper_widgets, TRUE);
    curl_global_cleanup();
    return 0;
}
