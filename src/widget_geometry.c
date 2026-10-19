#include "app.h"

void desk_widget_clamp_position(int screen_width, int screen_height, int widget_width, int widget_height, double *x, double *y) {
    double max_x = screen_width > 0 ? MAX(0.0, 1.0 - (double)widget_width / screen_width) : 1.0;
    double max_y = screen_height > 0 ? MAX(0.0, 1.0 - (double)widget_height / screen_height) : 1.0;
    *x = CLAMP(*x, 0.0, max_x); *y = CLAMP(*y, 0.0, max_y);
}
