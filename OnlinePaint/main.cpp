#include <stdlib.h>
#include <gtk/gtk.h>
#include <error.h>
#include <netdb.h>
#include <goocanvas-2.0/goocanvas.h>
#include <string.h>
#include <json.h>
#include <thread>
#include <fcntl.h>
#include <unordered_map>
#include <utility>

#define CANVAS_WIDTH 600
#define CANVAS_HEIGHT 600
#define DEFAULT_SIZE 100
#define ROTATE_FACTOR 1
#define MAX_ITEM_SIZE 600
#define SCROLL_SPEED 1.1
#define ZOOM_SPEED 4
#define MIN_SCALE 0.08
#define BUFFER_SIZE 1024
#define ITEM_STATE_NOTHING 0
#define ITEM_STATE_MOVE 1
#define ITEM_STATE_ROTATE 2
#define ITEM_STATE_SCALE 3
#define ITEM_STATE_EDIT_MOVE 301
#define ITEM_STATE_EDIT_ROTATE 302
#define ITEM_STATE_EDIT_SCALE 303
#define ITEM_STATE_EDIT_COLORIZE 304
#define ITEM_STATE_EDIT_DELETE 306
#define ITEM_STATE_EDIT_LOWER 100

std::thread c;

GtkWidget *buttonSquare = NULL;
GtkWidget *buttonCircle = NULL;
GtkWidget *buttonText = NULL;
GtkWidget *textContainter = NULL;
GtkWidget *textEdit = NULL;

GtkWidget *buttonDelete = NULL;
GtkWidget *buttonMove = NULL;
GtkWidget *buttonRotate = NULL;
GtkWidget *buttonScale = NULL;
GtkWidget *buttonColor = NULL;
GtkWidget *mainWin = NULL;
GtkWidget *box = NULL;
GtkWidget *canvas = NULL;
GtkWidget *list = NULL;
GtkWidget *panel = NULL;
GtkWidget *buttons = NULL;
GtkWidget *listScroll = NULL;
GtkWidget *colorChooser = NULL;
GdkCursor *cursor = NULL;
GooCanvasItem *root = NULL, *currentItem = NULL;

std::unordered_map<int, GooCanvasItem*> items;
std::unordered_map<int, json_object*> itemsJsons;
int sock;
bool quit = false;

struct timeval timeout;

int currentItemState = ITEM_STATE_NOTHING, currentItemId = -1;
bool itemBeingEdited = 0;
gdouble currentItemX, currentItemY, currentItemRotation, currentItemScale;
char *currentItemColor, *currentItemText, *currentItemFigure;


gdouble lastKnownY = 0;

constexpr unsigned int str2int(const char* str, int h = 0)
{
    return !str[h] ? 5381 : (str2int(str, h+1) * 33) ^ str[h];
}

void restoreSquare(json_object* jo);
void deleteItem(GooCanvasItem *item);
void moveItem(GooCanvasItem *item);
void scaleItem(GooCanvasItem *item);
void rotateItem(GooCanvasItem *item);
void colorizeItem(GooCanvasItem *item);
void restoreItemAttributes (GooCanvasItem* item);

void itemClick(GooCanvasItem  *item,
                GooCanvasItem  *target_item,
                GdkEventButton *event,
                gpointer        user_data)  {
    int id;
    const char *figure, *color, *text;
    switch(currentItemState) {
        case ITEM_STATE_EDIT_DELETE :
            printf("Deleting\n");
            deleteItem(item);
            break;
        case ITEM_STATE_EDIT_MOVE :
            printf("Moving\n");
            moveItem(item);
            break;
        case ITEM_STATE_EDIT_SCALE :
            printf("Scaling\n");
            scaleItem(item);
            break;
        case ITEM_STATE_EDIT_ROTATE :
            printf("Rotating\n");
            rotateItem(item);
            break;
        case ITEM_STATE_EDIT_COLORIZE :
            printf("Colorizing\n");
            colorizeItem(item);
            currentItemState = ITEM_STATE_NOTHING;
            gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
    }
}

void establishConnection(char* address, char* port) {
    timeout.tv_sec = 3;
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
	ssize_t bufsize1 = BUFFER_SIZE, received1, receivedTotal;
	char *buffer1, *temp;
	while(!quit) {
        receivedTotal = 0;
        temp = (char*) malloc(bufsize1 * sizeof(char));
        buffer1 = (char*) malloc(bufsize1 * sizeof(char));
        fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) & ~O_NONBLOCK);
        received1 = read(sock, temp, bufsize1);
        if(received1 > 0) {
            buffer1 = strncpy(buffer1, temp, received1);
            fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
            while(received1 > 0) {
                buffer1 = (char*) realloc(buffer1, (receivedTotal + received1 + 1) * sizeof(char));
                memcpy(buffer1 + receivedTotal, temp, received1);
                receivedTotal += received1;
                received1 = read(sock, (char*) temp, bufsize1);
            }
            try {
                printf("RECEIVED\n%s\n", buffer1);
                char *pch = strtok (buffer1,"\n");
                while(pch != NULL) {
                    json_object * jobj = json_tokener_parse(pch);

                    restoreSquare(jobj);
                    pch = strtok (NULL,"\n");
                }
            } catch(const char* msg) {
                printf("Unknown data receive errno %d message %s\n", errno, msg);
            }
        } else {
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
    json_object *jId = json_object_new_int(currentItemId);

    json_object_object_add(jObj,"figure", jFigure);
    json_object_object_add(jObj,"x", jX);
    json_object_object_add(jObj,"y", jY);
    json_object_object_add(jObj,"rotation", jRotation);
    json_object_object_add(jObj,"scale", jScale);
    json_object_object_add(jObj,"color", jColor);
    json_object_object_add(jObj,"text", jText);
    if(currentItemId > -1)
        json_object_object_add(jObj,"id", jId);

    const char* buf = json_object_to_json_string(jObj);

    int res = write(sock, buf, strlen(buf));
    if(res == -1) {
        printf("Connection lost\n");
        exit(0);
    }
    goo_canvas_item_remove(currentItem);  //we readd item when we receive it from the server

    currentItem = NULL;
}

void deleteItem(GooCanvasItem *item) {
    json_object *jObj, *jFigure, *jId;
    std::pair<int, GooCanvasItem*> now;
    const char* buf;
    int res;

    for(std::pair<int, GooCanvasItem*> i : items) {
        if(i.second == item)
            now = i;
    }

    jObj = json_object_new_object();
    jFigure = json_object_new_string("delete");
    jId = json_object_new_int(now.first);

    goo_canvas_item_remove(items[now.first]);
    itemsJsons.erase(now.first);
    items.erase(now.first);
    currentItemState = ITEM_STATE_NOTHING;
    gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);

    json_object_object_add(jObj,"figure", jFigure);
    json_object_object_add(jObj,"id", jId);

    buf = json_object_to_json_string(jObj);

    res = write(sock, buf, strlen(buf));
    if(res == -1) {
        printf("Connection lost\n");
        exit(0);
    }
}

void moveItem(GooCanvasItem *item) {
    restoreItemAttributes(item);
    currentItemState = ITEM_STATE_EDIT_MOVE;
}

void scaleItem(GooCanvasItem *item) {
    restoreItemAttributes(item);
    currentItemState = ITEM_STATE_EDIT_SCALE;
}

void rotateItem(GooCanvasItem *item) {
    restoreItemAttributes(item);
    currentItemState = ITEM_STATE_EDIT_ROTATE;
}

void colorizeItem(GooCanvasItem *item) {
    restoreItemAttributes(item);
    GdkRGBA color;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(colorChooser), &color);
    currentItemColor = gdk_rgba_to_string(&color);
    sendItem();
}

static void deleteItemCallback (GtkWidget *wid, GtkWidget *win) {
    printf("Delete item\n");
    if(currentItemState != ITEM_STATE_EDIT_DELETE) {
        currentItemState = ITEM_STATE_EDIT_DELETE;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
        cursor = gdk_cursor_new(GDK_X_CURSOR);
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), cursor);
    }
    else {
        currentItemState = ITEM_STATE_NOTHING;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
    }
}

static void moveItemCallback (GtkWidget *wid, GtkWidget *win) {
    if(currentItemState != ITEM_STATE_EDIT_MOVE) {
        currentItemState = ITEM_STATE_EDIT_MOVE;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
        cursor = gdk_cursor_new(GDK_FLEUR);
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), cursor);
    }
    else {
        currentItemState = ITEM_STATE_NOTHING;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
    }
}

static void scaleItemCallback (GtkWidget *wid, GtkWidget *win) {
    if(currentItemState != ITEM_STATE_EDIT_SCALE) {
        currentItemState = ITEM_STATE_EDIT_SCALE;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
        cursor = gdk_cursor_new(GDK_DOUBLE_ARROW);
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), cursor);
    }
    else {
        currentItemState = ITEM_STATE_NOTHING;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
    }
}

static void rotateItemCallback (GtkWidget *wid, GtkWidget *win) {
    if(currentItemState != ITEM_STATE_EDIT_ROTATE) {
        currentItemState = ITEM_STATE_EDIT_ROTATE;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
        cursor = gdk_cursor_new(GDK_EXCHANGE);
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), cursor);
    }
    else {
        currentItemState = ITEM_STATE_NOTHING;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
    }
}

static void colorizeItemCallback (GtkWidget *wid, GtkWidget *win) {
    if(currentItemState != ITEM_STATE_EDIT_COLORIZE) {
        currentItemState = ITEM_STATE_EDIT_COLORIZE;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
        cursor = gdk_cursor_new(GDK_SPRAYCAN);
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), cursor);
    }
    else {
        currentItemState = ITEM_STATE_NOTHING;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
    }
}

static void drawItem(char* figure, char* text, gdouble scale, gdouble rotation, char* color) {
    currentItemFigure = figure;
    currentItemText = text;
    currentItemScale = 1;
    currentItemRotation = 0;
    currentItemColor = color;
    if(currentItem) {
        goo_canvas_item_remove(currentItem);
    }
    switch(str2int(figure)) {
        case str2int("square") :
            currentItem = goo_canvas_rect_new (root, - DEFAULT_SIZE / 2, - DEFAULT_SIZE / 2, DEFAULT_SIZE, DEFAULT_SIZE,
                "line-width", 0.0,
                "radius-x", 0.0,
                "radius-y", 0.0,
                "fill-color", currentItemColor,
                NULL);
            break;
        case str2int("circle") :
            currentItem = goo_canvas_ellipse_new(root, - DEFAULT_SIZE / 2, - DEFAULT_SIZE / 2, DEFAULT_SIZE, DEFAULT_SIZE,
                "line-width", 0.0,
                "center-x", 0.0,
                "center-y", 0.0,
                "fill-color", currentItemColor,
                NULL);
        case str2int("text") :
            currentItem = goo_canvas_text_new (root, text, 0, 0, -1,
                GOO_CANVAS_ANCHOR_CENTER,
                "font", "Sans 24",
                "fill-color", currentItemColor,
                NULL);
            break;
    }
}

static void drawSquareCallback (GtkWidget *wid, GtkWidget *win) {
    if(currentItemState == ITEM_STATE_NOTHING || currentItemState > ITEM_STATE_EDIT_LOWER) {
        GdkRGBA color;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(colorChooser), &color);
        drawItem("square", "", 1, 0, gdk_rgba_to_string(&color));
        currentItemState = ITEM_STATE_MOVE;
    }
    else if(currentItemState < ITEM_STATE_EDIT_LOWER) {
        goo_canvas_item_remove(GOO_CANVAS_ITEM(currentItem));
        currentItem = NULL;
        currentItemState = ITEM_STATE_NOTHING;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
    }
}

static void drawCircleCallback (GtkWidget *wid, GtkWidget *win) {
    if(currentItemState == ITEM_STATE_NOTHING || currentItemState > ITEM_STATE_EDIT_LOWER) {
        GdkRGBA color;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(colorChooser), &color);
        drawItem("circle", "", 1, 0, gdk_rgba_to_string(&color));
        currentItemState = ITEM_STATE_MOVE;
    }
    else if(currentItemState < ITEM_STATE_EDIT_LOWER) {
        goo_canvas_item_remove(GOO_CANVAS_ITEM(currentItem));
        currentItem = NULL;
        currentItemState = ITEM_STATE_NOTHING;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
    }
}

static void drawTextCallback (GtkWidget *wid, GtkWidget *win) {
    if(currentItemState == ITEM_STATE_NOTHING || currentItemState > ITEM_STATE_EDIT_LOWER) {
        char* text = strdup(gtk_entry_get_text(GTK_ENTRY(textEdit)));
        if(strlen(text) > 0) {
            GdkRGBA color;
            gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(colorChooser), &color);
            drawItem("text", text, 1, 0, gdk_rgba_to_string(&color));
            currentItemState = ITEM_STATE_MOVE;
        }
    }
    else if(currentItemState < ITEM_STATE_EDIT_LOWER) {
        goo_canvas_item_remove(GOO_CANVAS_ITEM(currentItem));
        currentItem = NULL;
        currentItemState = ITEM_STATE_NOTHING;
        gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
    }
}

gboolean mouseMovedCallback(
                GtkWidget  *item,
                GdkEventMotion *event,
                gpointer        user_data) {
    gdouble left, top, right, bottom;
    goo_canvas_get_bounds(GOO_CANVAS(canvas), &left, &top, &right, &bottom);

    if(currentItem) {

        switch(currentItemState) {
            case ITEM_STATE_MOVE : case ITEM_STATE_EDIT_MOVE :
                goo_canvas_item_set_simple_transform(currentItem, event->x, event->y, currentItemScale, currentItemRotation);
                break;
            case ITEM_STATE_ROTATE : case ITEM_STATE_EDIT_ROTATE :
                currentItemRotation = ROTATE_FACTOR * (lastKnownY - event->y);
                goo_canvas_item_rotate(GOO_CANVAS_ITEM(currentItem), currentItemRotation, 0, 0);
                lastKnownY = event->y;
                break;
            case ITEM_STATE_SCALE : case ITEM_STATE_EDIT_SCALE :
                currentItemScale = (event->y / CANVAS_HEIGHT) * 2 * ZOOM_SPEED + MIN_SCALE;
                goo_canvas_item_set_simple_transform(GOO_CANVAS_ITEM(currentItem), currentItemX, currentItemY, currentItemScale, currentItemRotation);
                break;
        }
    }

    return false;
}


gboolean buttonReleaseCallback(
                GtkWidget  *item,
                GdkEventMotion *event,
                gpointer        user_data) {
    gdouble x, y, s;

    gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
    switch(currentItemState) {
        case ITEM_STATE_MOVE :
            if(currentItem) {
                lastKnownY = event->y;
                currentItemX = event->x;
                currentItemY = event->y;
//                if(!strcmp(currentItemFigure, "circle"))
//                    currentItemState = ITEM_STATE_SCALE;
//                else
                    currentItemState = ITEM_STATE_ROTATE;
            }
            break;
        case ITEM_STATE_ROTATE :
            if(currentItem) {
                goo_canvas_item_get_simple_transform(GOO_CANVAS_ITEM(currentItem), &x, &y, &s, &currentItemRotation);
                currentItemScale = 1;
                currentItemState = ITEM_STATE_SCALE;
            }
            break;
        case ITEM_STATE_SCALE :
            if(currentItem) {
                currentItemState = ITEM_STATE_NOTHING;
                gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
                sendItem();
                currentItemId = -1;
                currentItem = NULL;
                itemBeingEdited = 0;
            }
            break;
        case ITEM_STATE_EDIT_MOVE :
            if(currentItem) {
                lastKnownY = event->y;
                currentItemX = event->x;
                currentItemY = event->y;
                sendItem();
                currentItemId = -1;
                currentItem = NULL;
                itemBeingEdited = 0;
                currentItemState = ITEM_STATE_NOTHING;
                gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
            }
            break;
        case ITEM_STATE_EDIT_ROTATE :
            if(currentItem) {
                goo_canvas_item_get_simple_transform(GOO_CANVAS_ITEM(currentItem), &x, &y, &s, &currentItemRotation);
                //currentItemScale = ITEM_STATE_EDIT_SCALE;
                sendItem();
                currentItemId = -1;
                currentItem = NULL;
                itemBeingEdited = 0;
                currentItemState = ITEM_STATE_NOTHING;
                gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
            }
            break;
        case ITEM_STATE_EDIT_SCALE :
            if(currentItem) {
                currentItemState = ITEM_STATE_NOTHING;
                sendItem();
                currentItemId = -1;
                currentItem = NULL;
                itemBeingEdited = 0;
                currentItemState = ITEM_STATE_NOTHING;
                gdk_window_set_cursor(gtk_widget_get_window(mainWin), NULL);
            }
            break;
        default :
            break;
    }

    return false;
}

int main (int argc, char *argv[])
{
	if(argc != 3)
        error(1,0,"Need 2 args");

    establishConnection(argv[1], argv[2]);

    /* Initialize GTK+ */
    g_log_set_handler ("Gtk", G_LOG_LEVEL_WARNING, (GLogFunc) gtk_false, NULL);
    gtk_init (&argc, &argv);
    g_log_set_handler ("Gtk", G_LOG_LEVEL_WARNING, g_log_default_handler, NULL);

    /* Create the main window */
    mainWin = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_container_set_border_width (GTK_CONTAINER (mainWin), 8);
    gtk_window_set_title (GTK_WINDOW (mainWin), "Hello World");
    gtk_window_set_position (GTK_WINDOW (mainWin), GTK_WIN_POS_CENTER);
    gtk_widget_realize (mainWin);
    g_signal_connect (mainWin, "destroy", gtk_main_quit, NULL);

    /* Create a vertical box with buttons */
    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

    buttonSquare = gtk_button_new_with_label("Kwadrat");
    buttonCircle = gtk_button_new_with_label("Koło");

    buttonText = gtk_button_new_with_label("Tekst");
    textContainter = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    textEdit = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(textContainter), textEdit, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(textContainter), buttonText, TRUE, TRUE, 0);

    canvas = goo_canvas_new();

    gtk_widget_set_size_request(canvas, CANVAS_WIDTH, CANVAS_HEIGHT);
    goo_canvas_set_bounds(GOO_CANVAS(canvas), 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT);

    listScroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(listScroll, 200, -1);
    list = gtk_list_box_new();
    gtk_list_box_insert(GTK_LIST_BOX(list), buttonSquare, NULL);
    gtk_list_box_insert(GTK_LIST_BOX(list), buttonCircle, NULL);
    gtk_list_box_insert(GTK_LIST_BOX(list), textContainter, NULL);

    buttonDelete = gtk_button_new_with_label("Usuń");
    buttonMove = gtk_button_new_with_label("Przesuń");
    buttonRotate = gtk_button_new_with_label("Obróć");
    buttonScale = gtk_button_new_with_label("Skaluj");
    buttonColor = gtk_button_new_with_label("Zmień kolor");
    buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    gtk_box_pack_end(GTK_BOX(buttons), buttonDelete, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(buttons), buttonMove, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(buttons), buttonRotate, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(buttons), buttonScale, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(buttons), buttonColor, TRUE, TRUE, 0);


    panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    colorChooser = gtk_color_chooser_widget_new();
    gtk_box_pack_end(GTK_BOX(panel), buttons, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(panel), colorChooser, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(panel), listScroll, TRUE, TRUE, 0);
    gtk_widget_set_valign(GTK_WIDGET(buttonDelete), GTK_ALIGN_START);
    gtk_widget_set_halign(GTK_WIDGET(buttonDelete), GTK_ALIGN_START);
    gtk_widget_set_valign(GTK_WIDGET(buttonMove), GTK_ALIGN_START);
    gtk_widget_set_halign(GTK_WIDGET(buttonMove), GTK_ALIGN_START);
    gtk_widget_set_valign(GTK_WIDGET(buttonRotate), GTK_ALIGN_START);
    gtk_widget_set_halign(GTK_WIDGET(buttonRotate), GTK_ALIGN_START);
    gtk_widget_set_valign(GTK_WIDGET(buttonScale), GTK_ALIGN_START);
    gtk_widget_set_halign(GTK_WIDGET(buttonScale), GTK_ALIGN_START);
    gtk_widget_set_valign(GTK_WIDGET(buttonColor), GTK_ALIGN_START);
    gtk_widget_set_halign(GTK_WIDGET(buttonColor), GTK_ALIGN_START);

    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(listScroll), list);
    gtk_container_add ( GTK_CONTAINER (mainWin), box);
    gtk_box_pack_start(GTK_BOX(box), canvas, TRUE, TRUE, 0);
    gtk_box_pack_end ( GTK_BOX (box), panel, TRUE, TRUE, 0);


    root = goo_canvas_get_root_item(GOO_CANVAS(canvas));

    g_signal_connect (buttonSquare, "clicked", G_CALLBACK(drawSquareCallback), NULL);
    g_signal_connect (buttonCircle, "clicked", G_CALLBACK(drawCircleCallback), NULL);
    g_signal_connect (buttonText, "clicked", G_CALLBACK(drawTextCallback), NULL);
    g_signal_connect (buttonDelete, "clicked", G_CALLBACK(deleteItemCallback), NULL);
    g_signal_connect (buttonMove, "clicked", G_CALLBACK(moveItemCallback), NULL);
    g_signal_connect (buttonScale, "clicked", G_CALLBACK(scaleItemCallback), NULL);
    g_signal_connect (buttonRotate, "clicked", G_CALLBACK(rotateItemCallback), NULL);
    g_signal_connect (buttonColor, "clicked", G_CALLBACK(colorizeItemCallback), NULL);
    g_signal_connect(GOO_CANVAS(canvas), "motion-notify-event", G_CALLBACK(mouseMovedCallback), NULL);
    g_signal_connect(GOO_CANVAS(canvas), "button_release_event", G_CALLBACK(buttonReleaseCallback), NULL);

    c = std::thread(continousUpdate);

    /* Enter the main loop */
    gtk_widget_show_all (mainWin);
    gtk_main();

    quit = true;
    close(sock);
    c.join();
    return 0;
}

void restoreSquare(json_object* jo) {
	const char *figure, *color, *text;
	gdouble x, y, rotation, scale;
	int id;
    json_object_object_foreach(jo, key, val) {
        switch (str2int(key)) {
            case str2int("figure"):
                figure = json_object_get_string(val);
                break;
            case str2int("x"):
                x = json_object_get_double(val);
                break;
            case str2int("y"):
                y = json_object_get_double(val);
                break;
            case str2int("rotation"):
                rotation = json_object_get_double(val);
                break;
            case str2int("scale"):
                scale = json_object_get_double(val);
                break;
            case str2int("color") :
                color = json_object_get_string(val);
                break;
            case str2int("text"):
                text = json_object_get_string(val);
                break;
            case str2int("id"):
                id = json_object_get_int(val);
                break;
        }
    }
    printf("received figure %s id %d\n", figure, id);

    GooCanvasItem *newItem = NULL;
    switch(str2int(figure)) {
        case str2int("square") :
            newItem = goo_canvas_rect_new (root, - DEFAULT_SIZE / 2, - DEFAULT_SIZE / 2, DEFAULT_SIZE, DEFAULT_SIZE,
                                   "line-width", 0.0,
                                   "radius-x", 0.0,
                                   "radius-y", 0.0,
                                   "stroke-color", "yellow",
                                   "fill-color", color,
                                   NULL);
            goo_canvas_item_set_simple_transform(newItem, x, y, 1, 0);
            goo_canvas_item_rotate(newItem, rotation, 0, 0);
            goo_canvas_item_set_simple_transform(newItem, x, y, scale, rotation);
            break;
        case str2int("circle") :
            newItem = goo_canvas_ellipse_new(root, - DEFAULT_SIZE / 2, - DEFAULT_SIZE / 2, DEFAULT_SIZE, DEFAULT_SIZE,
                "line-width", 0.0,
                "center-x", 0.0,
                "center-y", 0.0,
                "fill-color", color,
                NULL);
            goo_canvas_item_set_simple_transform(newItem, x, y, 1, 0);
            goo_canvas_item_rotate(newItem, rotation, 0, 0);
            goo_canvas_item_set_simple_transform(newItem, x, y, scale, rotation);
            break;
        case str2int("text") :
            newItem = goo_canvas_text_new (root, text, 0, 0, -1,
                GOO_CANVAS_ANCHOR_CENTER,
                "font", "Sans 24",
                "fill-color", color,
                NULL);
            goo_canvas_item_set_simple_transform(newItem, x, y, 1, 0);
            goo_canvas_item_rotate(newItem, rotation, 0, 0);
            goo_canvas_item_set_simple_transform(newItem, x, y, scale, rotation);
            break;
    }

    if(newItem != NULL) {
        if(items.find(id) != items.end()) {
            goo_canvas_item_remove(items[id]);
        }
        items[id] = newItem;
        itemsJsons[id] = jo;
        g_signal_connect(GOO_CANVAS_ITEM(newItem), "button-press-event", G_CALLBACK(itemClick), NULL);
    }
}

void restoreItemAttributes (GooCanvasItem* item) {
    std::pair<int, GooCanvasItem*> now;
    int id;
    const char *figure, *color, *text;
	gdouble x, y, rotation, scale;
    for(std::pair<int, GooCanvasItem*> i : items) {
        if(i.second == item)
            now = i;
    }
    json_object_object_foreach(itemsJsons[now.first], key, val) {
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
            case str2int("id"):
                //printf("key: %s; value: %s\n", key, json_object_get_string(val));
                id = json_object_get_int(val);
                break;
        }
    }

    currentItem = item;
    currentItemId = now.first;

    currentItemX = x; currentItemY = y; currentItemRotation = rotation, currentItemScale = scale;
    currentItemColor = (char*) color;
    currentItemText = (char*) text;
    currentItemFigure = (char*) figure;

}
