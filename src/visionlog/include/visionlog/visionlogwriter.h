/***************************************************************************
 *   Copyright 2018 Tobias Heineken                                        *
 *   Robotics Erlangen e.V.                                                *
 *   http://www.robotics-erlangen.de/                                      *
 *   info@robotics-erlangen.de                                             *
 *                                                                         *
 *   This program is free software: you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation, either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifndef VISIONLOGWRITER_H
#define VISIONLOGWRITER_H

#include <QObject>
#include <QString>
#include <iostream>

#include "protobuf/ssl_detection.pb.h"

class VisionLogWriter: public QObject
{
    Q_OBJECT
public:
    explicit VisionLogWriter(const QString& filename);
    ~VisionLogWriter() override;
    void addVisionPacket(const SSL_DetectionFrame& data);
    void passTime();


private:
    std::ofstream* out_stream;
    qint64 time;
};


#endif //VISIONLOGWRITER_H