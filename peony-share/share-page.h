/*
 * Peony-Qt's Library
 *
 * Copyright (C) 2019, Tianjin KYLIN Information Technology Co., Ltd.
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

#ifndef SHAREPAGE_H
#define SHAREPAGE_H

#include <QWidget>
#include <properties-window-tab-iface.h>

#include "net-usershare-helper.h"

class SharePage : public Peony::PropertiesWindowTabIface
{
    Q_OBJECT
public:
    explicit SharePage(const QString &uri, QWidget *parent = nullptr);

    /*!
     * 响应确定按钮
     * \brief
     */
    void saveAllChange() override;

private:
    ShareInfo m_share_info = ShareInfo("", false);
};

#endif // SHAREPAGE_H
