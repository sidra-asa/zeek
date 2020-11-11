// See the file "COPYING" in the main distribution directory for copyright.

#pragma once

#include <utility>
#include <map>
#include <string_view>

#include "Plugin.h"
#include "Component.h"

#include "../Reporter.h"
#include "../ZeekArgs.h"

namespace zeek {
namespace plugin {

// Macros that trigger plugin hooks. We put this into macros to short-cut the
// code for the most common case that no plugin defines the hook.

/**
 * Macro to trigger hooks without result.
 *
 * @param hook The \a plugin::HookType constant corresponding to the hook to trigger.
 *
 * @param method_call The \a Manager method corresponding to the hook.
 */
#define PLUGIN_HOOK_VOID(hook, method_call) \
	{ if ( zeek::plugin_mgr->HavePluginForHook(zeek::plugin::hook) ) zeek::plugin_mgr->method_call; }

/**
 * Macro to trigger hooks that return a result.
 *
 * @param hook The \a plugin::HookType constant corresponding to the hook to trigger.
 *
 * @param method_call The \a Manager method corresponding to the hook.
 *
 * @param default_result: The result to use if there's no plugin implementing
 * the hook.
 */
#define PLUGIN_HOOK_WITH_RESULT(hook, method_call, default_result) \
	(zeek::plugin_mgr->HavePluginForHook(zeek::plugin::hook) ? zeek::plugin_mgr->method_call : (default_result))

/**
 * A singleton object managing all plugins.
 */
class Manager
{
public:
	typedef void (*bif_init_func)(Plugin *);
	using plugin_list = std::list<Plugin*>;
	using component_list = Plugin::component_list;
	using inactive_plugin_list = std::list<std::pair<std::string, std::string>>;

	/**
	 * Constructor.
	 */
	Manager();

	/**
	 * Destructor.
	 */
	virtual ~Manager();

	/**
	 * Request a plugin to be loaded. This only schedules the plugin for loading,
	 * the actual loading will happen later with ActivateDynamicPlugins().
	 */
	void RequestPlugin(std::string name) { requested_plugins.insert(std::move(name)); }

	/**
	 * Searches a set of directories for plugins. If a specified directory
	 * does not contain a plugin itself, the method searches for plugins
	 * recursively. For plugins found, the method makes them available for
	 * later activation via ActivatePlugin().
	 *
	 * This must be called only before InitPluginsPreScript().
	 *
	 * @param dir The directory to search for plugins. Multiple directories
	 * can be given by splitting them with ':'.
	 */
	void SearchDynamicPlugins(const std::string& dir);

	/**
	 * Activates plugins that were either explicitly requested or that
	 * SearchDynamicPlugins() has previously discovered. The effect is the same
	 * all calling \a ActivatePlugin(name) for each plugin. The function will
	 * abort with a fatal error if it cannot load all plugins as expected.
	 *
	 * @param all If true, activates all plugins that are found. If false,
	 * activates only those that should always be activated unconditionally,
	 * as specified via the ZEEK_PLUGIN_ACTIVATE environment variable. In other
	 * words, it's \c true in standard mode and \c false in bare mode.
	 *
	 * @return True if all plugins have been loaded successfully. If one
	 * fails to load, the method stops there without loading any further ones
	 * and returns false. Errors will have been reported.
	 */
	void ActivateDynamicPlugins(bool all);

	/**
	 * First-stage initializion of the manager. This is called early on
	 * during Bro's initialization, before any scripts are processed, and
	 * forwards to the corresponding Plugin methods.
	 */
	void InitPreScript();

	/**
	 * Second-stage initialization of the manager. This is called in between
	 * pre- and post-script to make BiFs available.
	 */
	void InitBifs();

	/**
	 * Third-stage initialization of the manager. This is called late during
	 * Bro's initialization after any scripts are processed, and forwards to
	 * the corresponding Plugin methods.
	 */
	void InitPostScript();

	/**
	 * Finalizes all plugins at termination time. This forwards to the
	 * corresponding Plugin \a Done() methods.
	 */
	void FinishPlugins();

	/**
	 * Returns a list of all available activated plugins. This includes all
	 * that are compiled in statically, as well as those loaded dynamically
	 * so far.
	 */
	plugin_list ActivePlugins() const;

	/**
	 * Returns a list of all dynamic plugins that have been found, yet not
	 * activated. The returned list contains pairs of plugin name and base
	 * directory. Note that because they aren't activated, that's all
	 * information we have access to.
	 */
	inactive_plugin_list InactivePlugins() const;

	/**
	 * Returns a list of all available components, in any plugin, that
	 * are derived from a specific class. The class is given as the
	 * template parameter \c T.
	 */
	template<class T> std::list<T *> Components() const;

	/**
	 * Returns the (dynamic) plugin associated with a given filesytem
	 * path. The path can be the plugin directory itself, or any path
	 * inside it.
	 */
	Plugin* LookupPluginByPath(std::string_view path);

	/**
	 * Returns true if there's at least one plugin interested in a given
	 * hook.
	 *
	 * @param The hook to check.
	 *
	 * @return True if there's a plugin for that hook.
	 */
	bool HavePluginForHook(HookType hook) const
		{
		// Inline to avoid the function call.
		return hooks[hook] != nullptr;
		}

	/**
	 * Returns all the hooks, with their priorities, that are currently
	 * enabled for a given plugin.
	 *
	 * @param plugin The plugin to return the hooks for.
	 */
	std::list<std::pair<HookType, int> > HooksEnabledForPlugin(const Plugin* plugin) const;

	/**
	 * Enables a hook for a given plugin.
	 *
	 * hook: The hook to enable.
	 *
	 * plugin: The plugin defining the hook.
	 *
	 * prio: The priority to associate with the plugin for this hook.
	 */
	void EnableHook(HookType hook, Plugin* plugin, int prio);

	/**
	 * Disables a hook for a given plugin.
	 *
	 * hook: The hook to enable.
	 *
	 * plugin: The plugin that used to define the hook.
	 */
	void DisableHook(HookType hook, Plugin* plugin);

	/**
	 * Registers interest in an event by a plugin, even if there's no handler
	 * for it. Normally a plugin receives events through HookQueueEvent()
	 * only if Bro actually has code to execute for it. By calling this
	 * method, the plugin tells Bro to raise the event even if there's no
	 * correspondong handler; it will then go into HookQueueEvent() just as
	 * any other.
	 *
	 * @param handler The event being interested in.
	 *
	 * @param plugin The plugin expressing interest.
	 */
	void RequestEvent(EventHandlerPtr handler, Plugin* plugin);

	/**
	 * Register interest in the destruction of a Obj instance. When Bro's
	 * reference counting triggers the objects destructor to run, the \a
	 * HookBroObjDtor will be called.
	 *
	 * @param handler The object being interested in.
	 *
	 * @param plugin The plugin expressing interest.
	 */
	void RequestBroObjDtor(Obj* obj, Plugin* plugin);

	// Hook entry functions.

	/**
	 * Hook that gives plugins a chance to take over loading an input
	 * file. This method must be called between InitPreScript() and
	 * InitPostScript() for each input file Bro is about to load, either
	 * given on the command line or via @load script directives. The hook can
	 * take over the file, in which case Bro must not further process it
	 * otherwise.
	 *
	 * @return 1 if a plugin took over the file and loaded it successfully; 0
	 * if a plugin took over the file but had trouble loading it; and -1 if
	 * no plugin was interested in the file at all.
	 */
	virtual int HookLoadFile(const Plugin::LoadType type, const std::string& file, const std::string& resolved);

	/**
	 * Hook that filters calls to a script function/event/hook.
	 *
	 * @param func The function to be called.
	 *
	 * @param parent The frame in which the function is being called.
	 *
	 * @param args The function call's arguments; they may be modified by the
	 * method.
	 *
	 * @return If a plugin handled the call, a Val representing the result
	 * to pass back to the interpreter (for void functions and events,
	 * it may be any Val and must be ignored). If no plugin handled the call,
	 * the method returns null.
	 */
	std::pair<bool, ValPtr>
	HookCallFunction(const Func* func, zeek::detail::Frame* parent, Args* args) const;

	/**
	 * Hook that filters the queuing of an event.
	 *
	 * @param event The event to be queued; it may be modified.
	 *
	 * @return Returns true if a plugin handled the queuing; in that case
	 * the plugin will have taken ownership.
	 */
	bool HookQueueEvent(Event* event) const;

	/**
	 * Hook that informs plugins about an update in network time.
	 *
	 * @param network_time The new network time.
	 */
	void HookUpdateNetworkTime(double network_time) const;

	/**
	 * Hook that executes when a connection's initial analyzer tree
	 * has been fully set up. The hook can manipulate the tree at this time,
	 * for example by adding further analyzers.
	 *
	 * @param conn The connection.
	 */
	void HookSetupAnalyzerTree(Connection *conn) const;

	/**
	 * Hook that informs plugins that the event queue is being drained.
	 */
	void HookDrainEvents() const;

	/**
	 * Hook that informs plugins that an BroObj is being destroyed. Will be
	 * called only for objects that a plugin has expressed interest in.
	 */
	void HookBroObjDtor(void* obj) const;

	/**
	 * Hook into log initialization. This method will be called when a
	 * logging writer is created. A writer represents a single logging
	 * filter. The method is called in the main thread, on the node that
	 * causes a log line to be written. It will _not_ be called on the logger
	 * node. The function will be called once for every instantiated writer.
	 *
	 * @param writer The name of the writer being instantiated.
	 *
	 * @param instantiating_filter Name of the filter causing the
	 *        writer instantiation.
	 *
	 * @param local True if the filter is logging locally (writer
	 *              thread will be located in same process).
	 *
	 * @param remote True if filter is logging remotely (writer thread
	 *               will be located in different thread, typically
	 *               in manager or logger node).
	 *
	 * @param info WriterBackend::WriterInfo with information about the writer.
	 *
	 * @param num_fields number of fields in the record being written.
	 *
	 * @param fields threading::Field description of the fields being logged.
	 */
	void HookLogInit(const std::string& writer,
	                 const std::string& instantiating_filter,
	                 bool local, bool remote,
	                 const logging::WriterBackend::WriterInfo& info,
	                 int num_fields,
	                 const threading::Field* const* fields) const;

	/**
	 * Hook into log writing. This method will be called for each log line
	 * being written by each writer. Each writer represents a single logging
	 * filter. The method is called in the main thread, on the node that
	 * causes a log line to be written. It will _not_ be called on the logger
	 * node.
	 * This function allows plugins to modify or skip logging of information.
	 * Note - once a log line is skipped (by returning false), it will not
	 * passed on to hooks that have not yet been called.
	 *
	 * @param writer The name of the writer.
	 *
	 * @param filter Name of the filter being written to.
	 *
	 * @param info WriterBackend::WriterInfo with information about the writer.
	 *
	 * @param num_fields number of fields in the record being written.
	 *
	 * @param fields threading::Field description of the fields being logged.
	 *
	 * @param vals threading::Values containing the values being written. Values
	 *             can be modified in the Hook.
	 *
	 * @return true if log line should be written, false if log line should be
	 *         skipped and not passed on to the writer.
	 */
	bool HookLogWrite(const std::string& writer,
	                  const std::string& filter,
	                  const logging::WriterBackend::WriterInfo& info,
	                  int num_fields, const threading::Field* const* fields,
	                  threading::Value** vals) const;

	/**
	 * Hook into reporting. This method will be called for each reporter call
	 * made; this includes weirds. The method cannot manipulate the data at
	 * the current time; however it is possible to prevent script-side events
	 * from being called by returning false.
	 *
	 * @param prefix The prefix passed by the reporter framework
	 *
	 * @param event The event to be called
	 *
	 * @param conn The associated connection
	 *
	 * @param addl Additional Bro values; typically will be passed to the event
	 *             by the reporter framework.
	 *
	 * @param location True if event expects location information
	 *
	 * @param location1 First location
	 *
	 * @param location2 Second location
	 *
	 * @param time True if event expects time information
	 *
	 * @param message Message supplied by the reporter framework
	 *
	 * @return true if event should be called by the reporter framework, false
	 *         if the event call should be skipped
	 */
	bool HookReporter(const std::string& prefix, const EventHandlerPtr event,
	                  const Connection* conn, const ValPList* addl, bool location,
	                  const zeek::detail::Location* location1, const zeek::detail::Location* location2,
	                  bool time, const std::string& message);

	/**
	 * Internal method that registers a freshly instantiated plugin with
	 * the manager.
	 *
	 * @param plugin The plugin to register. The method does not take
	 * ownership, yet assumes the pointer will stay valid at least until
	 * the Manager is destroyed.
	 */
	static void RegisterPlugin(Plugin* plugin);

	/**
	 * Internal method that registers a bif file's init function for a
	 * plugin.
	 *
	 * @param plugin The plugin to register the function for.
	 *
	 * @param c The init function to register.
	 */
	static void RegisterBifFile(const char* plugin, bif_init_func c);

private:
	bool ActivateDynamicPluginInternal(const std::string& name, bool ok_if_not_found, std::vector<std::string>* errors);
	void UpdateInputFiles();
	void MetaHookPre(HookType hook, const HookArgumentList& args) const;
	void MetaHookPost(HookType hook, const HookArgumentList& args, HookArgument result) const;

	// Plugins explicitly requested to be activated.
	std::set<std::string> requested_plugins;

	 // All found dynamic plugins, mapping their names to base directory.
	using dynamic_plugin_map = std::map<std::string, std::string>;
	dynamic_plugin_map dynamic_plugins;

	// We temporarliy buffer scripts to load to get them to load in the
	// right order.
	using file_list = std::list<std::string>;
	file_list scripts_to_load;

	bool init;	// Flag indicating whether InitPreScript() has run yet.

	// A hook list keeps pairs of plugin and priority interested in a
	// given hook.
	using hook_list = std::list<std::pair<int, Plugin*>>;

	// An array indexed by HookType. An entry is null if there's no hook
	// of that type enabled.
	hook_list** hooks;

	// A map of all the top-level plugin directories.
	std::map<std::string, Plugin*> plugins_by_path;

	// Helpers providing access to current state during dlopen().
	static Plugin* current_plugin;
	static const char* current_dir;
	static const char* current_sopath;

	// Returns a modifiable list of all plugins, both static and dynamic.
	// This is a static method so that plugins can register themselves
	// even before the manager exists.
	static plugin_list* ActivePluginsInternal();

	using bif_init_func_list = std::list<bif_init_func>;
	using bif_init_func_map = std::map<std::string, bif_init_func_list*>;

	// Returns a modifiable map of all bif files. This is a static method
	// so that plugins can register their bifs even before the manager
	// exists.
	static bif_init_func_map* BifFilesInternal();
};

template<class T>
std::list<T *> Manager::Components() const
	{
	std::list<T *> result;

	for ( plugin_list::const_iterator p = ActivePluginsInternal()->begin(); p != ActivePluginsInternal()->end(); p++ )
		{
		component_list components = (*p)->Components();

		for ( component_list::const_iterator c = components.begin(); c != components.end(); c++ )
			{
			T* t = dynamic_cast<T *>(*c);

			if ( t )
				result.push_back(t);
			}
		}

	return result;
	}

namespace detail {

/**
 * Internal class used by bifcl-generated code to register its init functions at runtime.
 */
class __RegisterBif {
public:
	__RegisterBif(const char* plugin, Manager::bif_init_func init)
		{
		Manager::RegisterBifFile(plugin, init);
		}
};

} // namespace detail
} // namespace plugin

extern plugin::Manager* plugin_mgr;

} // namespace zeek

namespace plugin {

using Manager [[deprecated("Remove in v4.1. Use zeek::plugin::Manager.")]] = zeek::plugin::Manager;

} // namespace plugin

/**
 * The global plugin manager singleton.
 */
extern zeek::plugin::Manager*& plugin_mgr [[deprecated("Remove in v4.1. Use zeek::plugin_mgr.")]];
