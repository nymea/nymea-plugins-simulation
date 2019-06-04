TEMPLATE = subdirs

PLUGIN_DIRS = \
    celsios                \


# Verify if building only a selection of plugins
contains(CONFIG, selection) {
    # Check each plugin if the subdir exists
    for(plugin, PLUGINS) {
        contains(PLUGIN_DIRS, $${plugin}) {
            SUBDIRS*= $${plugin}
        } else {
            error("Invalid plugin passed. There is no subdirectory with the name $${plugin}.")
        }
    }
    message("Building plugin selection: $${SUBDIRS}")
} else {
    SUBDIRS *= $${PLUGIN_DIRS}
    message("Building all plugins")
}

