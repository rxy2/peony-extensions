/*
 * Peony-Qt's Library
 *
 * Copyright (C) 2020, Tianjin KYLIN Information Technology Co., Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors: Yue Lan <lanyue@kylinos.cn>
 *
 */
#include "computer-volume-item.h"
#include <peony-qt/file-utils.h>
#include "computer-model.h"
#include <QMessageBox>
#include <QDebug>
#include <QApplication>
#include <QPushButton>

ComputerVolumeItem::ComputerVolumeItem(GVolume *volume, ComputerModel *model, AbstractComputerItem *parentNode, QObject *parent) : AbstractComputerItem(model, parentNode, parent)
{
    if (parentNode->itemType() != Volume) {
        m_displayName = tr("Volume");
        return;
    }

    m_cancellable = g_cancellable_new();
    if (!volume) {
        m_icon = QIcon::fromTheme("drive-harddisk-system");
        m_uri = "file:///";
        m_displayName = tr("File System");
        auto file = g_file_new_for_uri("file:///");
        g_file_query_filesystem_info_async(file, "*", 0, m_cancellable,
                                           GAsyncReadyCallback(query_root_info_async_callback), this);
        return;
    }

    m_volume = std::make_shared<Peony::Volume>(volume, true);

    updateInfoAsync();

    g_signal_connect(volume, "changed", G_CALLBACK(volume_changed_callback), this);
    g_signal_connect(volume, "removed", G_CALLBACK(volume_removed_callback), this);
}

ComputerVolumeItem::~ComputerVolumeItem()
{
    g_cancellable_cancel(m_cancellable);
    g_object_unref(m_cancellable);
}

void ComputerVolumeItem::updateInfoAsync()
{
    char *deviceName;
    QString unixDeviceName;

    m_displayName = m_volume->name();
    m_icon = QIcon::fromTheme(m_volume->iconName());
    //qDebug()<<m_displayName;

    check();

    auto mount = g_volume_get_mount(m_volume->getGVolume());
    if (mount) {
        m_mount = std::make_shared<Peony::Mount>(mount, true);
        auto active_root = g_mount_get_root(mount);
        if (active_root) {
            auto uri = g_file_get_uri(active_root);
            if (uri) {
                m_uri = uri;
                g_free(uri);
            }
            g_file_query_filesystem_info_async(active_root, "*",
                                               0, m_cancellable, GAsyncReadyCallback(qeury_info_async_callback), this);
            g_object_unref(active_root);
        }
    } else {
        //m_mount = nullptr;
        //mount first
        //FIXME: check auto mount
//        this->mount();
    }

    //Handle the Chinese name of fat32 udisk.
    deviceName = g_volume_get_identifier(m_volume->getGVolume(),G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
    if(deviceName){
        unixDeviceName = QString(deviceName);
        Peony::FileUtils::handleVolumeLabelForFat32(m_displayName,unixDeviceName);
        g_free(deviceName);
    }

    auto index = this->itemIndex();
    m_model->dataChanged(index, index);
}

const QString ComputerVolumeItem::displayName()
{
    return m_displayName;
}

const QIcon ComputerVolumeItem::icon()
{
    return m_icon.isNull()? AbstractComputerItem::icon(): m_icon;
}

bool ComputerVolumeItem::isMount()
{
    return Peony::FileUtils::isMountPoint(m_uri);
}

void ComputerVolumeItem::findChildren()
{
    //add root
    m_model->beginInsertItem(this->itemIndex(), m_children.count());
    auto root = new ComputerVolumeItem(nullptr, m_model, this);
    m_children<<root;
    m_model->endInsterItem();

    //enumerate
    auto volume_monitor = g_volume_monitor_get();
    auto current_volumes = g_volume_monitor_get_volumes(volume_monitor);
    GList *l = current_volumes;
    while (l) {
        auto volume = G_VOLUME(l->data);
        m_model->beginInsertItem(this->itemIndex(), m_children.count());
        auto item = new ComputerVolumeItem(volume, m_model, this);
        m_children<<item;
        m_model->endInsterItem();
        l = l->next;
    }

    //monitor
    auto volumeManager = Peony::VolumeManager::getInstance();
    connect(volumeManager, &Peony::VolumeManager::volumeAdded, this, &ComputerVolumeItem::onVolumeAdded);
}

void ComputerVolumeItem::check()
{
    if (!m_volume)
        return;

    auto active_root = g_volume_get_activation_root(m_volume->getGVolume());
    if (active_root) {
        auto uri = g_file_get_uri(active_root);
        auto path = g_file_get_path(active_root);
        //QMessageBox::information(0, 0, QString("%1 has active root %2").arg(m_displayName).arg(uri));
        if (uri) {
            /*!
              \bug
              when a volume added, it could have active root represond with a uri,
              such as mtp://, etc...
              however it could not get the local path of active root correctly at the
              time. that makes some volumes location changement meet troubles.
              */
            m_uri = uri;

            g_free(uri);
        }
        if (path) {
            m_uri = QString("file://%1").arg(path);
            g_free(path);
        }
        g_object_unref(active_root);
    }

    if (m_uri.isNull()) {
        GFile* file = nullptr;
        GMount* mount = g_volume_get_mount(m_volume->getGVolume());
        if (nullptr != mount) {
            file = g_mount_get_root (mount);
            if (nullptr != file) {
                m_uri = g_file_get_uri(file);
            }
        }
        if (nullptr != file) {
            g_object_unref(file);
        }
        if (nullptr != mount) {
            g_object_unref (mount);
        }
    }
}

bool ComputerVolumeItem::canEject()
{
    GVolume *gvolume;
    GDrive  *gdrive;
    bool ejectAble = false;

    if("file:///" == m_uri)    /*The root File System cannot eject*/
        return false;

    if(NULL != m_volume->getGVolume()){
        gvolume = (GVolume*)g_object_ref(m_volume->getGVolume());
        gdrive = g_volume_get_drive(gvolume);
        if(gdrive){
            ejectAble = g_drive_can_eject(gdrive);
            g_object_unref(gdrive);
        }
        g_object_unref(gvolume);
    }

    return ejectAble;
}

void ComputerVolumeItem::eject(GMountUnmountFlags ejectFlag)
{
    GVolume *g_volume;
    GMount *g_mount;

    /*If udisk device is mounted,eject it from here*/
    if (m_mount && (g_mount = m_mount->getGMount())) {
        g_mount_eject_with_operation(g_mount, ejectFlag, nullptr, m_cancellable,
                                     GAsyncReadyCallback(eject_async_callback), this);

        return;
    }

    /*If udisk device is unmounted,eject it from here*/
    if (m_volume && (g_volume = m_volume->getGVolume())) {
        g_volume_eject_with_operation(g_volume, ejectFlag, nullptr, m_cancellable,
                                     GAsyncReadyCallback(eject_async_callback), this);
    }

}

bool ComputerVolumeItem::canUnmount()
{
    return m_mount != nullptr;
}

void ComputerVolumeItem::unmount(GMountUnmountFlags unmountFlag)
{
    if (auto g_mount = m_mount->getGMount()) {
        g_mount_unmount_with_operation(g_mount, unmountFlag, nullptr, m_cancellable,
                                       GAsyncReadyCallback(unmount_async_callback), this);
    }
}

void ComputerVolumeItem::mount()
{
    if (m_uri == "file:///") {
        return;
    }

    if (m_mount) {
        auto root = g_mount_get_root(m_mount->getGMount());
        if (root) {
            auto uri = g_file_get_uri(root);
            if (uri) {
                m_uri = uri;
                g_free(uri);
            }
            g_file_query_filesystem_info_async(root, "*",
                                               0, m_cancellable, GAsyncReadyCallback(qeury_info_async_callback), this);
            g_object_unref(root);
        }
    } else {
        g_volume_mount(m_volume->getGVolume(),
                       G_MOUNT_MOUNT_NONE,
                       nullptr,
                       m_cancellable,
                       GAsyncReadyCallback(mount_async_callback),
                       this);
    }
}

QModelIndex ComputerVolumeItem::itemIndex()
{
    if (!m_parentNode) {
        return m_model->createItemIndex(0, this);
    } else {
        return m_model->createItemIndex(m_parentNode->m_children.indexOf(this), this);
    }
}

void ComputerVolumeItem::volume_changed_callback(GVolume *volume, ComputerVolumeItem *p_this)
{
    //QMessageBox::information(0, 0, tr("Volume Changed"));
    p_this->m_mount = nullptr;
    p_this->m_uri = nullptr;
    p_this->m_icon = QIcon();
    p_this->m_displayName = nullptr;
    p_this->m_usedSpace = 0;
    p_this->m_totalSpace = 0;
    p_this->updateInfo();
}

void ComputerVolumeItem::volume_removed_callback(GVolume *volume, ComputerVolumeItem *p_this)
{
    auto parentNode = p_this->m_parentNode;
    if (!parentNode)
        return;

    auto row = parentNode->m_children.indexOf(p_this);
    parentNode->m_model->beginRemoveItem(parentNode->itemIndex(), row);
    parentNode->m_children.removeAt(row);
    p_this->deleteLater();
    parentNode->m_model->endRemoveItem();
}

void ComputerVolumeItem::qeury_info_async_callback(GFile *file, GAsyncResult *res, ComputerVolumeItem *p_this)
{
    GError *err = nullptr;
    auto info = g_file_query_info_finish(file, res, &err);
    if (info) {
        quint64 total = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
        quint64 used = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_USED);
        p_this->m_totalSpace = total;
        p_this->m_usedSpace = used;
        auto index = p_this->itemIndex();
        p_this->m_model->dataChanged(index, index);
        //p_this->m_model->dataChanged(p_this->itemIndex(), p_this->itemIndex());

        g_object_unref(info);
    }
    if (err) {
        //QMessageBox::critical(0, 0, err->message);
        g_error_free(err);
    }
}

void ComputerVolumeItem::query_root_info_async_callback(GFile *file, GAsyncResult *res, ComputerVolumeItem *p_this)
{
    GError *err = nullptr;
    GFileInfo *info = g_file_query_info_finish(file, res, &err);
    if (info) {
        quint64 total = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
        quint64 used = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_USED);
        p_this->m_totalSpace = total;
        p_this->m_usedSpace = used;
        auto index = p_this->itemIndex();
        p_this->m_model->dataChanged(index, index);
        //p_this->m_model->dataChanged(p_this->itemIndex(), p_this->itemIndex());

        g_object_unref(info);
    }
    if (err) {
        //QMessageBox::critical(0, 0, err->message);
        g_error_free(err);
    }
}

void ComputerVolumeItem::mount_async_callback(GVolume *volume, GAsyncResult *res, ComputerVolumeItem *p_this)
{
    GError *err = nullptr;
    bool successed = g_volume_mount_finish(volume, res, &err);
    if (err) {
        //QMessageBox::critical(0, 0, err->message);
        g_error_free(err);
    }

    if (successed) {
        p_this->updateInfoAsync();
    }
}

void ComputerVolumeItem::unmount_async_callback(GMount *mount, GAsyncResult *res, ComputerVolumeItem *p_this)
{
    GError *err = nullptr;
    QString errorMsg;
    bool successed = g_mount_unmount_with_operation_finish(mount, res, &err);
    if (successed) {
        //QMessageBox::information(0, 0, "Volume Umounted");
        p_this->m_mount = nullptr;
    }
    if (err) {
        if(strstr(err->message,"target is busy"))
            errorMsg = QObject::tr("One or more programs prevented the unmount operation.");

        auto button = QMessageBox::warning(nullptr, QObject::tr("Unmount failed"),
                                           QObject::tr("Error: %1\n"
                                                       "Do you want to unmount forcely?").arg(errorMsg),
                                           QMessageBox::Yes, QMessageBox::No);
        if (button == QMessageBox::Yes)
            p_this->unmount(G_MOUNT_UNMOUNT_FORCE);

        g_error_free(err);
    }
}

void ComputerVolumeItem::eject_async_callback(GObject *object, GAsyncResult *res, ComputerVolumeItem *p_this)
{
    GError *err = nullptr;
    QString errorMsg;
    bool successed;

    if(G_IS_MOUNT(object))
        successed = g_mount_eject_with_operation_finish(G_MOUNT(object), res, &err);
    else if(G_IS_VOLUME(object))
        successed = g_volume_eject_with_operation_finish(G_VOLUME(object), res, &err);

    if (successed) {
        //QMessageBox::information(0, 0, "Volume Ejected");
    }
    if (err) {
        //QMessageBox::critical(0, 0, err->message);
        QMessageBox warningBox(QMessageBox::Warning,QObject::tr("Eject failed"),QString(err->message));
        QPushButton *cancelBtn = (warningBox.addButton(QObject::tr("Cancel"),QMessageBox::RejectRole));
        QPushButton *ensureBtn = (warningBox.addButton(QObject::tr("Eject Anyway"),QMessageBox::YesRole));
        warningBox.exec();
        if(warningBox.clickedButton() == ensureBtn)
            p_this->eject(G_MOUNT_UNMOUNT_FORCE);

        g_error_free(err);
    }
}

void ComputerVolumeItem::onVolumeAdded(const std::shared_ptr<Peony::Volume> volume)
{
    auto g_volume = volume->getGVolume();
    m_model->beginInsertItem(this->itemIndex(), m_children.count());
    auto item = new ComputerVolumeItem(g_volume, m_model, this);
    m_children<<item;
    m_model->endInsterItem();
}
