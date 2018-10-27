/*
Launchy: Application Launcher
Copyright (C) 2007-2009  Josh Karlin

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "precompiled.h"
#include "plugin_handler.h"
#include "GlobalVar.h"
#include "catalog_types.h"
#include "SettingsManager.h"

int PluginInfo::sendMessage(int msgId, void* wParam, void* lParam)
{
    // This should have some kind of exception guard to prevent
    // Launchy from crashing when a plugin is misbehaving.
    // This would consist of a try/catch block to handle C++ exceptions
    // and on Windows would also include a structured exception handler
    return obj->msg(msgId, wParam, lParam);
}


PluginHandler::PluginHandler()
{
}


PluginHandler::~PluginHandler()
{
}


void PluginHandler::showLaunchy() {
    foreach(PluginInfo info, plugins) {
        if (info.loaded)
            info.sendMessage(MSG_LAUNCHY_SHOW);
    }
}


void PluginHandler::hideLaunchy() {
    foreach(PluginInfo info, plugins) {
        if (info.loaded)
            info.sendMessage(MSG_LAUNCHY_HIDE);
    }
}


void PluginHandler::getLabels(QList<InputData>* inputData)
{
    if (inputData->count() > 0)
    {
        foreach(PluginInfo info, plugins)
        {
            if (info.loaded)
                info.sendMessage(MSG_GET_LABELS, (void*)inputData);
        }
    }
}


void PluginHandler::getResults(QList<InputData>* inputData, QList<CatItem>* results)
{
    if (inputData->count() > 0)
    {
        foreach(PluginInfo info, plugins)
        {
            if (info.loaded)
                info.sendMessage(MSG_GET_RESULTS, (void*)inputData, (void*)results);
        }
    }
}


void PluginHandler::getCatalogs(Catalog* catalog, INotifyProgressStep* progressStep)
{
    int index = 0;

    foreach(PluginInfo info, plugins)
    {
        if (info.loaded)
        {
            QList<CatItem> items;
            info.sendMessage(MSG_GET_CATALOG, (void*)&items);
            foreach(CatItem item, items)
            {
                catalog->addItem(item);
            }
            if (progressStep)
            {
                progressStep->progressStep(index);
            }
            ++index;
        }
    }
}


int PluginHandler::execute(QList<InputData>* inputData, CatItem* result)
{
    if (!plugins.contains(result->id) || !plugins[result->id].loaded)
        return 0;
    return plugins[result->id].sendMessage(MSG_LAUNCH_ITEM, (void*)inputData, (void*)result);
}


QWidget* PluginHandler::doDialog(QWidget* parent, uint id)
{
    if (!plugins.contains(id) || !plugins[id].loaded)
        return NULL;
    QWidget* newBox = NULL;
    plugins[id].sendMessage(MSG_DO_DIALOG, (void*)parent, (void*)&newBox);
    return newBox;
}


void PluginHandler::endDialog(uint id, bool accept)
{
    if (!plugins.contains(id) || !plugins[id].loaded)
        return;
    plugins[id].sendMessage(MSG_END_DIALOG, (void*)accept);
}


void PluginHandler::loadPlugins() {
    // Get the list of loadable plugins
    QHash<uint, bool> loadable;

    int size = g_settings->beginReadArray("plugins");
    for (int i = 0; i < size; ++i)
    {
        g_settings->setArrayIndex(i);
        uint id = g_settings->value("id").toUInt();
        bool toLoad = g_settings->value("load").toBool();
        loadable[id] = toLoad;
    }
    g_settings->endArray();

    foreach(QString directory, SettingsManager::instance().directory("plugins"))
    {
        // Load up the plugins in the plugins/ directory
        QDir pluginsDir(directory);
        foreach(QString fileName, pluginsDir.entryList(QDir::Files))
        {
            if (!QLibrary::isLibrary(fileName))
                continue;
            QPluginLoader loader(pluginsDir.absoluteFilePath(fileName));
            qDebug() << "Loading plugin" << fileName;
            QObject *plugin = loader.instance();
            if (!plugin)
            {
                qWarning() << fileName << "is not a plugin";
                continue;
            }
            PluginInterface *plug = qobject_cast<PluginInterface *>(plugin);
            if (!plug)
            {
                qWarning() << fileName << "is not a Launchy plugin";
                continue;
            }

            qDebug() << "Plugin loaded";

            plug->settings = &g_settings;
            PluginInfo info;
            info.obj = plug;
            info.path = pluginsDir.absoluteFilePath(fileName);
            bool handled = info.sendMessage(MSG_GET_ID, (void*)&info.id) != 0;
            info.sendMessage(MSG_GET_NAME, (void*)&info.name);

            if (handled && (!loadable.contains(info.id) || loadable[info.id]))
            {
                info.loaded = true;
                info.sendMessage(MSG_INIT);
                info.sendMessage(MSG_PATH, &directory);

                // Load any of the plugin's plugins of its own
                QList<PluginInfo> additionalPlugins;
                info.sendMessage(MSG_LOAD_PLUGINS, &additionalPlugins);

                foreach(PluginInfo pluginInfo, additionalPlugins)
                {
                    if (!pluginInfo.isValid())
                    {
                        continue;
                    }

                    bool isPluginLoadable =
                        !loadable.contains(pluginInfo.id) || loadable[pluginInfo.id];

                    if (isPluginLoadable)
                    {
                        pluginInfo.sendMessage(MSG_INIT);
                        pluginInfo.loaded = true;
                    }
                    else
                    {
                        pluginInfo.sendMessage(MSG_UNLOAD_PLUGIN, (void*)pluginInfo.id);
                        pluginInfo.loaded = false;
                    }
                    plugins[pluginInfo.id] = pluginInfo;
                }
            }
            else
            {
                info.loaded = false;
                loader.unload();
            }
            plugins[info.id] = info;
        }
    }
}
