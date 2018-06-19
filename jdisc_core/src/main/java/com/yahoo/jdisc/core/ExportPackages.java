// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.jdisc.core;

import org.apache.felix.framework.util.Util;
import org.osgi.framework.Constants;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.util.Properties;
import java.util.jar.JarInputStream;

/**
 * @author Simon Thoresen Hult
 */
public class ExportPackages {

    public static final String PROPERTIES_FILE = "/exportPackages.properties";
    public static final String EXPORT_PACKAGES = "exportPackages";
    private static final String REPLACE_VERSION_PREFIX = "__REPLACE_VERSION__";

    public static void main(String[] args) throws IOException {
        String fileName = args[0];
        if (!fileName.endsWith(PROPERTIES_FILE)) {
            throw new IllegalArgumentException("Expected '" + PROPERTIES_FILE + "', got '" + fileName + "'.");
        }
        StringBuilder out = new StringBuilder();
        out.append(getSystemPackages()).append(",")
           .append("com.sun.security.auth,")
           .append("com.sun.security.auth.module,")
           .append("com.sun.management,")
           .append("com.yahoo.jdisc,")
           .append("com.yahoo.jdisc.application,")
           .append("com.yahoo.jdisc.handler,")
           .append("com.yahoo.jdisc.service,")
           .append("com.yahoo.jdisc.statistics,")
           .append("javax.inject;version=1.0.0,")  // Included in guice, but not exported. Needed by container-jersey.
           .append("org.aopalliance.intercept,")
           .append("org.aopalliance.aop,")

           .append("sun.misc,")
           .append("sun.net.util,")
           .append("sun.security.krb5,")

           // xml-apis:xml-apis:1.4.01 is not a bundle
           .append("org.w3c.dom,")
           .append("org.w3c.dom.bootstrap,")
           .append("org.w3c.dom.css,")
           .append("org.w3c.dom.events,")
           .append("org.w3c.dom.html,")
           .append("org.w3c.dom.ls,")
           .append("org.w3c.dom.ranges,")
           .append("org.w3c.dom.stylesheets,")
           .append("org.w3c.dom.traversal,")
           .append("org.w3c.dom.views");

        for (int i = 1; i < args.length; ++i) {
            out.append(",").append(getExportedPackages(args[i]));
        }
        Properties props = new Properties();
        props.setProperty(EXPORT_PACKAGES, out.toString());

        try (FileWriter writer = new FileWriter(new File(fileName))) {
            props.store(writer, "generated by " + ExportPackages.class.getName());
        }
    }

    public static String readExportProperty() {
        Properties props = new Properties();
        try {
            props.load(ExportPackages.class.getResourceAsStream(PROPERTIES_FILE));
        } catch (IOException e) {
            throw new IllegalStateException("Failed to read resource '" + PROPERTIES_FILE + "'.");
        }
        return props.getProperty(EXPORT_PACKAGES);
    }

    public static String getSystemPackages() {
        return Util.getDefaultProperty(null, "org.osgi.framework.system.packages");
    }

    private static String getExportedPackages(String argument) throws IOException {
        if (argument.startsWith(REPLACE_VERSION_PREFIX)) {
            String jarFile = argument.substring(REPLACE_VERSION_PREFIX.length());
            return readExportHeader(jarFile);
        } else {
            return readExportHeader(argument);
        }
    }

    private static String readExportHeader(String jarFile) throws IOException {
        try (JarInputStream jar = new JarInputStream(new FileInputStream(jarFile))) {
            return jar.getManifest().getMainAttributes().getValue(Constants.EXPORT_PACKAGE);
        }
    }
}
