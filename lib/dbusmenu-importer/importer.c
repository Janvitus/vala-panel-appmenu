#include "importer.h"
#include "dbusmenu-interface.h"
#include "model.h"

struct _DBusMenuImporter
{
	GObject parent_instance;
	char *bus_name;
	char *object_path;
	ulong name_id;
	GCancellable *cancellable;
	DBusMenuXml *proxy;
	DBusMenuModel *top_model;
	GSimpleActionGroup *all_actions;
};

enum
{
	PROP_NULL,
	PROP_BUS_NAME,
	PROP_OBJECT_PATH,
	PROP_MODEL,
	LAST_PROP
};

static GParamSpec *properties[LAST_PROP] = { NULL };
G_DEFINE_TYPE(DBusMenuImporter, dbus_menu_importer, G_TYPE_OBJECT)

static void proxy_ready_cb(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	DBusMenuXml *proxy      = dbus_menu_xml_proxy_new_finish(res, &error);

	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		return;

	DBusMenuImporter *menu = (DBusMenuImporter *)(user_data);
	menu->proxy            = proxy;

	if (error)
	{
		g_warning("%s", error->message);
		return;
	}
	menu->top_model =
	    dbus_menu_model_new(0, NULL, menu->proxy, G_ACTION_GROUP(menu->all_actions));
	g_object_notify_by_pspec(menu, properties[PROP_MODEL]);
}

static void name_appeared_cb(GDBusConnection *connection, const gchar *name,
                             const gchar *name_owner, gpointer user_data)
{
	DBusMenuImporter *menu = (DBusMenuImporter *)(user_data);

	dbus_menu_xml_proxy_new(connection,
	                        G_DBUS_PROXY_FLAGS_NONE,
	                        menu->bus_name,
	                        menu->object_path,
	                        menu->cancellable,
	                        proxy_ready_cb,
	                        menu);
}

static void name_vanished_cb(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	DBusMenuImporter *menu = (DBusMenuImporter *)(user_data);

	g_clear_object(&menu->proxy);
	g_clear_object(&menu->top_model);
	g_object_notify_by_pspec(menu, properties[PROP_MODEL]);
}

static void dbus_menu_importer_constructed(GObject *object)
{
	G_OBJECT_CLASS(dbus_menu_importer_parent_class)->constructed(object);
	DBusMenuImporter *menu = (DBusMenuImporter *)(object);

	menu->name_id = g_bus_watch_name(G_BUS_TYPE_SESSION,
	                                 menu->bus_name,
	                                 G_BUS_NAME_WATCHER_FLAGS_NONE,
	                                 name_appeared_cb,
	                                 name_vanished_cb,
	                                 menu,
	                                 NULL);
}

static void dbus_menu_importer_dispose(GObject *object)
{
	DBusMenuImporter *menu = (DBusMenuImporter *)(object);

	if (menu->name_id > 0)
	{
		g_bus_unwatch_name(menu->name_id);
		menu->name_id = 0;
	}
	g_cancellable_cancel(menu->cancellable);
	g_clear_object(&menu->cancellable);
	g_clear_object(&menu->proxy);
	g_clear_object(&menu->top_model);
	g_clear_object(&menu->all_actions);

	G_OBJECT_CLASS(dbus_menu_importer_parent_class)->dispose(object);
}

static void dbus_menu_importer_finalize(GObject *object)
{
	DBusMenuImporter *menu = (DBusMenuImporter *)(object);

	g_clear_pointer(&menu->bus_name, g_free);
	g_clear_pointer(&menu->object_path, g_free);

	G_OBJECT_CLASS(dbus_menu_importer_parent_class)->finalize(object);
}

static void dbus_menu_importer_set_property(GObject *object, guint property_id, const GValue *value,
                                            GParamSpec *pspec)
{
	DBusMenuImporter *menu = (DBusMenuImporter *)(object);

	switch (property_id)
	{
	case PROP_BUS_NAME:
		menu->bus_name = g_value_dup_string(value);
		break;

	case PROP_OBJECT_PATH:
		menu->object_path = g_value_dup_string(value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void dbus_menu_importer_get_property(GObject *object, guint property_id, GValue *value,
                                            GParamSpec *pspec)
{
	DBusMenuImporter *menu = (DBusMenuImporter *)(object);
	switch (property_id)
	{
	case PROP_MODEL:
		g_value_set_object(value, menu->top_model);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void install_properties(GObjectClass *object_class)
{
	properties[PROP_BUS_NAME] =
	    g_param_spec_string("bus-name",
	                        "bus-name",
	                        "bus-name",
	                        NULL,
	                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

	properties[PROP_OBJECT_PATH] =
	    g_param_spec_string("object-path",
	                        "object-path",
	                        "object-path",
	                        NULL,
	                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);
	properties[PROP_MODEL] = g_param_spec_object("model",
	                                             "model",
	                                             "model",
	                                             G_TYPE_MENU_MODEL,
	                                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties(object_class, LAST_PROP, properties);
}

static void dbus_menu_importer_class_init(DBusMenuImporterClass *menu_class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(menu_class);

	object_class->constructed  = dbus_menu_importer_constructed;
	object_class->dispose      = dbus_menu_importer_dispose;
	object_class->finalize     = dbus_menu_importer_finalize;
	object_class->set_property = dbus_menu_importer_set_property;
	object_class->get_property = dbus_menu_importer_get_property;

	install_properties(object_class);
}

static void dbus_menu_importer_init(DBusMenuImporter *menu)
{
	menu->proxy       = NULL;
	menu->top_model   = NULL;
	menu->cancellable = g_cancellable_new();
	menu->all_actions = g_simple_action_group_new();
}

DBusMenuImporter *dbus_menu_importer_new(const gchar *bus_name, const gchar *object_path)
{
	return g_object_new(dbus_menu_importer_get_type(),
	                    "bus-name",
	                    bus_name,
	                    "object-path",
	                    object_path,
	                    NULL);
}