/*
 * Copyright (C) 2018 Boudewijn Rempt <boud@valdyas.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "KisResourceLocator.h"

#include <QApplication>
#include <QDebug>
#include <QList>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMessageBox>
#include <QVersionNumber>

#include <QSqlQuery>
#include <QSqlError>

#include <kconfig.h>
#include <kconfiggroup.h>
#include <ksharedconfig.h>
#include <klocalizedstring.h>

#include <KritaVersionWrapper.h>
#include "KoResourcePaths.h"
#include "KisResourceStorage.h"
#include "KisResourceCacheDb.h"
#include "KisResourceLoaderRegistry.h"

const QString KisResourceLocator::resourceLocationKey {"ResourceDirectory"};


class KisResourceLocator::Private {
public:
    QString resourceLocation;
    QMap<QString, KisResourceStorageSP> storages;
    QHash<QPair<QString, QString>, KoResourceSP> resourceCache;
    QStringList errorMessages;
};

KisResourceLocator::KisResourceLocator(QObject *parent)
    : QObject(parent)
    , d(new Private())
{
}

KisResourceLocator *KisResourceLocator::instance()
{
    KisResourceLocator *locator = qApp->findChild<KisResourceLocator *>(QString());
    if (!locator) {
        locator = new KisResourceLocator(qApp);
    }
    return locator;
}

KisResourceLocator::~KisResourceLocator()
{
}

KisResourceLocator::LocatorError KisResourceLocator::initialize(const QString &installationResourcesLocation)
{
    InitalizationStatus initalizationStatus = InitalizationStatus::Unknown;

    KConfigGroup cfg(KSharedConfig::openConfig(), "");
    d->resourceLocation = cfg.readEntry(resourceLocationKey, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

    QFileInfo fi(d->resourceLocation);

    if (!fi.exists()) {
        if (!QDir().mkpath(d->resourceLocation)) {
            d->errorMessages << i18n("1. Could not create the resource location at %1.", d->resourceLocation);
            return LocatorError::CannotCreateLocation;
        }
        initalizationStatus = InitalizationStatus::FirstRun;
    }

    if (!fi.isWritable()) {
        d->errorMessages << i18n("2. The resource location at %1 is not writable.", d->resourceLocation);
        return LocatorError::LocationReadOnly;
    }

    // Check whether we're updating from an older version
    if (initalizationStatus != InitalizationStatus::FirstRun) {
        QFile fi(d->resourceLocation + '/' + "KRITA_RESOURCE_VERSION");
        if (!fi.exists()) {
            initalizationStatus = InitalizationStatus::FirstUpdate;
        }
        else {
            fi.open(QFile::ReadOnly);
            QVersionNumber resource_version = QVersionNumber::fromString(QString::fromUtf8(fi.readAll()));
            QVersionNumber krita_version = QVersionNumber::fromString(KritaVersionWrapper::versionString());
            if (krita_version > resource_version) {
                initalizationStatus = InitalizationStatus::Updating;
            }
            else {
                initalizationStatus = InitalizationStatus::Initialized;
            }
        }
    }

    if (initalizationStatus != InitalizationStatus::Initialized) {
        KisResourceLocator::LocatorError res = firstTimeInstallation(initalizationStatus, installationResourcesLocation);
        if (res != LocatorError::Ok) {
            return res;
        }
        initalizationStatus = InitalizationStatus::Initialized;
    }
    else {
        if (!synchronizeDb()) {
            return LocatorError::CannotSynchronizeDb;
        }
    }
    return LocatorError::Ok;
}

QStringList KisResourceLocator::errorMessages() const
{
    return d->errorMessages;
}

QString KisResourceLocator::resourceLocationBase() const
{
    return d->resourceLocation;
}

bool KisResourceLocator::resourceCached(QString storageLocation, const QString &resourceLocation) const
{
    if (storageLocation.isEmpty()) {
        storageLocation = resourceLocationBase();
    }
    else {
        storageLocation = resourceLocationBase() + '/' + storageLocation;
    }

    QPair<QString, QString> key = QPair<QString, QString> (storageLocation, resourceLocation);

    return d->resourceCache.contains(key);
}

KoResourceSP KisResourceLocator::resource(QString storageLocation, const QString &resourceLocation)
{
    if (storageLocation.isEmpty()) {
        storageLocation = resourceLocationBase();
    }
    else {
        storageLocation = resourceLocationBase() + '/' + storageLocation;
    }

    QPair<QString, QString> key = QPair<QString, QString> (storageLocation, resourceLocation);

    KoResourceSP resource;
    if (d->resourceCache.contains(key)) {
        resource = d->resourceCache[key];
    }
    else {
        Q_ASSERT(d->storages.contains(storageLocation));
        KisResourceStorageSP storage = d->storages[storageLocation];
        Q_ASSERT(storage);

        resource = storage->resource(resourceLocation);
        if (resource) {
            d->resourceCache[key] = resource;
        }
    }
    return resource;
}

bool KisResourceLocator::removeResource(int resourceId)
{
    // First remove the resource from the cache

    // XXX: Should this query go into KisResourceCacheDb?
    QSqlQuery q;
    bool r = q.prepare("SELECT storages.location\n"
                       ",      resources.filename\n"
                       "FROM   resources\n"
                       ",      storages\n"
                       "WHERE  resources.id = :resource_id\n"
                       "AND    resources.storage_id = storages.id");
    if (!r) {
        qWarning() << "KisResourceLocator::removeResource: could not prepare query." << q.lastError();
        return false;
    }

    q.bindValue(":resource_id", resourceId);

    r = q.exec();
    if (!r) {
        qWarning() << "KisResourceLocator::removeResource: could not execute query." << q.lastError();
    }

    q.first();

    QString storageLocation = q.value("location").toString();
    QString resourceLocation = q.value("filename").toString();

    if (storageLocation.isEmpty()) {
        storageLocation = resourceLocationBase();
    }
    else {
        storageLocation = resourceLocationBase() + '/' + storageLocation;
    }

    QPair<QString, QString> key = QPair<QString, QString> (storageLocation, resourceLocation);
    r = d->resourceCache.remove(key);

    return KisResourceCacheDb::removeResource(resourceId);
}

KisResourceLocator::LocatorError KisResourceLocator::firstTimeInstallation(InitalizationStatus initalizationStatus, const QString &installationResourcesLocation)
{

    emit progressMessage(i18n("Krita is running for the first time. Intialization will take some time."));
    Q_UNUSED(initalizationStatus);

    Q_FOREACH(const QString &folder, KisResourceLoaderRegistry::instance()->resourceTypes()) {
        QDir dir(d->resourceLocation + '/' + folder + '/');
        if (!dir.exists()) {
            if (!QDir().mkpath(d->resourceLocation + '/' + folder + '/')) {
                d->errorMessages << i18n("3. Could not create the resource location at %1.", dir.path());
                return LocatorError::CannotCreateLocation;
            }
        }
    }

    Q_FOREACH(const QString &folder, KisResourceLoaderRegistry::instance()->resourceTypes()) {
        QDir dir(installationResourcesLocation + '/' + folder + '/');
        if (dir.exists()) {
            Q_FOREACH(const QString &entry, dir.entryList(QDir::Files | QDir::Readable)) {
                QFile f(dir.canonicalPath() + '/'+ entry);
                if (!QFileInfo(d->resourceLocation + '/' + folder + '/' + entry).exists()) {
                    if (!f.copy(d->resourceLocation + '/' + folder + '/' + entry)) {
                        d->errorMessages << i18n("Could not copy resource %1 to %2").arg(f.fileName()).arg(d->resourceLocation + '/' + folder + '/' + entry);
                    }
                }
            }
        }
    }

    // And add bundles and adobe libraries
    QStringList filters = QStringList() << "*.bundle" << "*.abr" << "*.asl";
    QDirIterator iter(installationResourcesLocation, filters, QDir::Files, QDirIterator::Subdirectories);
    while (iter.hasNext()) {
        iter.next();
        emit progressMessage(i18n("Installing the resources from bundle %1.", iter.filePath()));
        QFile f(iter.filePath());
        Q_ASSERT(f.exists());
        if (!f.copy(d->resourceLocation + '/' + iter.fileName())) {
            d->errorMessages << i18n("Could not copy resource %1 to %2").arg(f.fileName()).arg(d->resourceLocation);
        }
    }

    QFile f(d->resourceLocation + '/' + "KRITA_RESOURCE_VERSION");
    f.open(QFile::WriteOnly);
    f.write(KritaVersionWrapper::versionString().toUtf8());
    f.close();

    if (!initializeDb()) {
        return LocatorError::CannotInitializeDb;
    }

    return LocatorError::Ok;
}

bool KisResourceLocator::initializeDb()
{
    emit progressMessage(i18n("Initalizing the resources."));
    d->errorMessages.clear();
    findStorages();

    Q_FOREACH(KisResourceStorageSP storage, d->storages) {

        if (!KisResourceCacheDb::addStorage(storage, (storage->type() == KisResourceStorage::StorageType::Folder ? false : true))) {
            d->errorMessages.append(i18n("Could not add storage %1 to the cache database").arg(storage->location()));
        }

        Q_FOREACH(const QString &resourceType, KisResourceLoaderRegistry::instance()->resourceTypes()) {
            emit progressMessage(i18n("Adding %1 resources to folder %2", resourceType, storage->location()));
            if (!KisResourceCacheDb::addResources(storage, resourceType)) {
                d->errorMessages.append(i18n("Could not add resource type %1 to the cache database").arg(resourceType));
            }
            if (!KisResourceCacheDb::addTags(storage, resourceType)) {
                d->errorMessages.append(i18n("Could not add tags for resource type %1 to the cache database").arg(resourceType));
            }
        }
    }

    return (d->errorMessages.isEmpty());
}

void KisResourceLocator::findStorages()
{
    d->storages.clear();

    // Add the folder
    KisResourceStorageSP storage = QSharedPointer<KisResourceStorage>::create(d->resourceLocation);
    d->storages[storage->location()] = storage;

    // And add bundles and adobe libraries
    QStringList filters = QStringList() << "*.bundle" << "*.abr" << "*.asl";
    QDirIterator iter(d->resourceLocation, filters, QDir::Files, QDirIterator::Subdirectories);
    while (iter.hasNext()) {
        iter.next();
        KisResourceStorageSP storage = QSharedPointer<KisResourceStorage>::create(iter.filePath());
        d->storages[storage->location()] = storage;
    }
}

QList<KisResourceStorageSP> KisResourceLocator::storages() const
{
    return d->storages.values();
}

bool KisResourceLocator::synchronizeDb()
{
    d->errorMessages.clear();
    findStorages();
    Q_FOREACH(const KisResourceStorageSP storage, d->storages) {
        if (!KisResourceCacheDb::synchronizeStorage(storage)) {
            d->errorMessages.append(i18n("Could not synchronize %1 with the database").arg(storage->location()));
        }
    }
    return d->errorMessages.isEmpty();
}
