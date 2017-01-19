#include <stdlib.h>
#include <gtk/gtk.h>
#include <error.h>
#include <netdb.h>
#include <goocanvas-2.0/goocanvas.h>
#include <string.h>
#include <json.h>
#include <thread>

#define CANVAS_WIDTH 600
#define CANVAS_HEIGHT 600
#define DEFAULT_SIZE 100
#define ROTATE_FACTOR 1
#define MAX_ITEM_SIZE 600
#define SCROLL_SPEED 1.1
#define ZOOM_SPEED 4
#define MIN_SCALE 0.08
#define BUFFER_SIZE 1024

std::thread c;

GtkWidget *button = NULL;
GtkWidget *win = NULL;
GtkWidget *box = NULL, *buttons = NULL;
GtkWidget *canvas = NULL;
GtkWidget *list = NULL;
GtkWidget *panel = NULL;
GtkWidget *listScroll = NULL;
GtkWidget *colorChooser = NULL;
GooCanvasItem *root = NULL, *currentItem = NULL, *text_item = NULL;
int sock;
bool quit = false;
typedef enum
{
  SQUARE,
  RECTANGLE,
  CIRCLE,
  TEXT
} figures;

int currentItemState = 0;
gdouble currentItemX, currentItemY, currentItemRotation, currentItemScale;
char *currentItemColor, *currentItemText, *currentItemFigure;


gdouble lastKnownY = 0;

void restoreSquare(const char* color, gdouble x, gdouble y, gdouble rotation, gdouble scale);

constexpr unsigned int str2int(const char* str, int h = 0)
{
    return !str[h] ? 5381 : (str2int(str, h+1) * 33) ^ str[h];
}

void establishConnection(char* address, char* port) {
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

	// Resolve arguments to IPv4 address with a port number
	addrinfo *resolved, hints={.ai_flags=0, .ai_family=AF_INET, .ai_socktype=SOCK_STREAM};
	int res = getaddrinfo(address, port, &hints, &resolved);
	if(res || !resolved) error(1, errno, "getaddrinfo");

	// create socket
	sock = socket(resolved->ai_family, resolved->ai_socktype, 0);
	if(sock == -1) error(1, errno, "socket failed");

	// attept to connect
	res = connect(sock, resolved->ai_addr, resolved->ai_addrlen);
	if(res) error(1, errno, "connect failed");

	// free memory
	freeaddrinfo(resolved);

    if (setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
//        error("setsockopt failed\n");
        printf("sockopt 1 failed\n");
    }
    if (setsockopt (sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
//        error("setsockopt failed\n");
        printf("sockopt 2 failed\n");
    }
}

void continousUpdate() {
	ssize_t bufsize1 = BUFFER_SIZE, received1;
	gdouble x, y, rotation, scale;
	const char *figure, *color, *text;
	char buffer1[bufsize1];
	while(!quit) {
        printf("Continous update\n");
        received1 = read(sock, buffer1, bufsize1);
        if(received1 > 0) {
            try {
                printf("Received %s\n", buffer1);
                char *pch = strtok (buffer1,"\n");
                while(pch != NULL) {
                    printf("PARSING %s\n", pch);
                    x = 0; y = 0; rotation = 0; scale = 0; figure = ""; color = ""; text = "";
                    json_object * jobj = json_tokener_parse(pch);
                    json_object_object_foreach(jobj, key, val) {
                        switch (str2int(key)) {
                            case str2int("figure"):
                                //printf("key: %s; value: %s\n", key, json_object_get_string(val));
                                figure = json_object_get_string(val);
                                break;
                            case str2int("x"):
                                //printf("key: %s; value: %s\n", key, json_object_get_double(val));
                                x = json_object_get_double(val);
                                break;
                            case str2int("y"):
                                //printf("key: %s; value: %s\n", key, json_object_get_double(val));
                                y = json_object_get_double(val);
                                break;
                            case str2int("rotation"):
                                //printf("key: %s; value: %s\n", key, json_object_get_double(val));
                                rotation = json_object_get_double(val);
                                break;
                            case str2int("scale"):
                                //printf("key: %s; value: %s\n", key, json_object_get_double(val));
                                scale = json_object_get_double(val);
                                break;
                            case str2int("color") :
                                //printf("key: %s; value: %s\n", key, json_object_get_string(val));
                                color = json_object_get_string(val);
                                break;
                            case str2int("text"):
                                //printf("key: %s; value: %s\n", key, json_object_get_string(val));
                                text = json_object_get_string(val);
                                break;
                        }
                    }
                    printf("received figure %s\n", figure);
                    switch (str2int(figure)) {
                        case str2int("square") :
                            restoreSquare(color, x, y, rotation, scale);
                            break;
                        default :
                            printf("received unknown figure %s\n", figure);
                            break;
                    }
                    pch = strtok (NULL,"\n");
                }
            } catch(const char* msg) {
                printf("Unknown data receive errno %d message %s\n", errno, msg);
            }
        } else {
            printf("received %d\n", (int)received1);
            int wrote1 = write(sock, "{}", strlen("{}"));
            if(wrote1 == -1) {
                printf("Connection lost\n");
                exit(0);
            }
        }
    }
}

void sendItem() {
    /*Creating a json object*/
    json_object * jObj = json_object_new_object();
    json_object *jFigure = json_object_new_string(currentItemFigure);
    json_object *jX = json_object_new_double(currentItemX);
    json_object *jY = json_object_new_double(currentItemY);
    json_object *jRotation = json_object_new_double(currentItemRotation);
    json_object *jScale = json_object_new_double(currentItemScale);
    json_object *jColor = json_object_new_string(currentItemColor);
    json_object *jText = json_object_new_string(currentItemText);

    json_object_object_add(jObj,"figure", jFigure);
    json_object_object_add(jObj,"x", jX);
    json_object_object_add(jObj,"y", jY);
    json_object_object_add(jObj,"rotation", jRotation);
    json_object_object_add(jObj,"scale", jScale);
    json_object_object_add(jObj,"color", jColor);
    json_object_object_add(jObj,"text", jText);

    const char* buf = json_object_to_json_string(jObj);

    int res = write(sock, buf, strlen(buf));
    if(res == -1) {
        printf("Connection lost\n");
        exit(0);
    }

    /*Now printing the json object*/
    printf ("The json object created: %s\nLength %d\n", buf, (int) strlen(buf));

    //    restoreSquare(currentItemColor, currentItemX, currentItemY, currentItemRotation, currentItemScale);
}

static void drawSquare (GtkWidget *wid, GtkWidget *win)
{
    currentItemFigure = "square";
    currentItemText = "";
    GdkRGBA color;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(colorChooser), &color);
    currentItemColor = gdk_rgba_to_string(&color);
    if(currentItem) {
        goo_canvas_item_remove(currentItem);
    }
    currentItem = goo_canvas_rect_new (root, - DEFAULT_SIZE / 2, - DEFAULT_SIZE / 2, DEFAULT_SIZE, DEFAULT_SIZE,
                               "line-width", 0.0,
                               "radius-x", 0.0,
                               "radius-y", 0.0,
                               "stroke-color", "yellow",
                               "fill-color", currentItemColor,
                               NULL);
   currentItemState = 0;
}

gboolean mouseMoved(
                GtkWidget  *item,
                GdkEventMotion *event,
                gpointer        user_data) {
    gdouble left, top, right, bottom;
    goo_canvas_get_bounds(GOO_CANVAS(canvas), &left, &top, &right, &bottom);
    //printf("Mouse moved %f %f %d!\n", event->x, event->y, currentItemState);

    if(currentItem) {
        switch(currentItemState) {
            case 0 :
                goo_canvas_item_set_simple_transform(currentItem, event->x, event->y, 1, 0);
                break;
            case 1 :
                currentItemRotation = ROTATE_FACTOR * (lastKnownY - event->y);
                goo_canvas_item_rotate(GOO_CANVAS_ITEM(currentItem), currentItemRotation, 0, 0);
                lastKnownY = event->y;
                break;
            case 2 :
                currentItemScale = (event->y / CANVAS_HEIGHT) * 2 * ZOOM_SPEED + MIN_SCALE;
                goo_canvas_item_set_simple_transform(GOO_CANVAS_ITEM(currentItem), currentItemX, currentItemY, currentItemScale, currentItemRotation);
                break;
        }
    } else {
        currentItemState = 0;
    }

    return false;
}

gboolean buttonPressed(
                GtkWidget  *item,
                GdkEventMotion *event,
                gpointer        user_data) {
    printf("Button pressed %f %f!\n", event->x, event->y);

    return false;
}

gboolean buttonRelease(
                GtkWidget  *item,
                GdkEventMotion *event,
                gpointer        user_data) {
    gdouble x, y, s;

    if(currentItem) {
        switch(currentItemState) {
            case 0 :
                lastKnownY = event->y;
                currentItemX = event->x;
                currentItemY = event->y;
                currentItemState = 1;
                printf("Now state 1\n");
                break;
            case 1 :
                goo_canvas_item_get_simple_transform(GOO_CANVAS_ITEM(currentItem), &x, &y, &s, &currentItemRotation);
                currentItemScale = 1;
                currentItemState = 2;
                printf("Now state 2 currentItemRotation %f\n", currentItemRotation);
                break;
            case 2 :
                currentItemState = 0;
                sendItem();
                currentItem = NULL;

                printf("Now state 0 currentItemScale %f\n", currentItemScale);
        }
    }
    else {
        currentItemState = 0;
    }
    return false;
}

gboolean zoom(GtkWidget  *item,
                GdkEventScroll *event,
                gpointer        user_data) {
    printf("Zoom direction %d\n", event->direction);
    if(currentItem) {
        switch(event->direction) {
            case GDK_SCROLL_UP :
                goo_canvas_item_scale(GOO_CANVAS_ITEM(currentItem), SCROLL_SPEED, SCROLL_SPEED);
                break;
            case GDK_SCROLL_DOWN :
//                goo_canvas_item_set_simple_transform(GOO_CANVAS_ITEM(currentItem), -1, -1, )
                goo_canvas_item_scale(GOO_CANVAS_ITEM(currentItem), 1/SCROLL_SPEED, 1/SCROLL_SPEED);

                break;
            }
    } else {
        currentItemState = 0;
    }
    return false;
}

int main (int argc, char *argv[])
{
	if(argc!=3)
        error(1,0,"Need 2 args");

    establishConnection(argv[1], argv[2]);

    /* Initialize GTK+ */
    g_log_set_handler ("Gtk", G_LOG_LEVEL_WARNING, (GLogFunc) gtk_false, NULL);
    gtk_init (&argc, &argv);
    g_log_set_handler ("Gtk", G_LOG_LEVEL_WARNING, g_log_default_handler, NULL);

    /* Create the main window */
    win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_container_set_border_width (GTK_CONTAINER (win), 8);
    gtk_window_set_title (GTK_WINDOW (win), "Hello World");
    gtk_window_set_position (GTK_WINDOW (win), GTK_WIN_POS_CENTER);
    gtk_widget_realize (win);
    g_signal_connect (win, "destroy", gtk_main_quit, NULL);

    /* Create a vertical box with buttons */
    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

    button = gtk_button_new_with_label("Kwadrat");

    canvas = goo_canvas_new();

    gtk_widget_set_size_request(canvas, CANVAS_WIDTH, CANVAS_HEIGHT);
    goo_canvas_set_bounds(GOO_CANVAS(canvas), 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT);

    listScroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(listScroll, 200, -1);
    list = gtk_list_box_new();
    gtk_list_box_insert(GTK_LIST_BOX(list), button, NULL);

    panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    colorChooser = gtk_color_chooser_widget_new();
    gtk_box_pack_end(GTK_BOX(panel), colorChooser, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(panel), listScroll, TRUE, TRUE, 0);

    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(listScroll), list);
    gtk_container_add ( GTK_CONTAINER (win), box);
    gtk_box_pack_start(GTK_BOX(box), canvas, TRUE, TRUE, 0);
    gtk_box_pack_end ( GTK_BOX (box), panel, TRUE, TRUE, 0);


    root = goo_canvas_get_root_item(GOO_CANVAS(canvas));

    text_item = goo_canvas_text_new (root, "Hello World", 300, 300, -1,
                                   GOO_CANVAS_ANCHOR_CENTER,
                                   "font", "Sans 24",
                                   NULL);
    goo_canvas_item_rotate (text_item, 45, 300, 300);

    g_signal_connect (button, "clicked", G_CALLBACK(drawSquare), NULL);
    g_signal_connect(GOO_CANVAS(canvas), "motion-notify-event", G_CALLBACK(mouseMoved), NULL);
    //g_signal_connect(GOO_CANVAS(canvas), "button_press_event", G_CALLBACK(buttonPressed), NULL);
    g_signal_connect(GOO_CANVAS(canvas), "button_release_event", G_CALLBACK(buttonRelease), NULL);
    //g_signal_connect(GOO_CANVAS(canvas), "scroll-event", G_CALLBACK(zoom), NULL);

    c = std::thread(continousUpdate);

    /* Enter the main loop */
    gtk_widget_show_all (win);
    gtk_main();

    quit = true;
    close(sock);
    c.join();
    return 0;
}

void restoreSquare(const char* color, gdouble x, gdouble y, gdouble rotation, gdouble scale) {
    GooCanvasItem *newItem = goo_canvas_rect_new (root, - DEFAULT_SIZE / 2, - DEFAULT_SIZE / 2, DEFAULT_SIZE, DEFAULT_SIZE,
                           "line-width", 0.0,
                           "radius-x", 0.0,
                           "radius-y", 0.0,
                           "stroke-color", "yellow",
                           "fill-color", color,
                           NULL);
    goo_canvas_item_set_simple_transform(newItem, x, y, 1, 0);
    goo_canvas_item_rotate(newItem, rotation, 0, 0);
    goo_canvas_item_set_simple_transform(newItem, x, y, scale, rotation);
}
