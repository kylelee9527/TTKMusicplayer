#include "musicmvradioprogramrequest.h"
#include "musickgqueryinterface.h"
#include "musicsemaphoreloop.h"
#include "musicnumberutils.h"
#include "musiccoreutils.h"

MusicMVRadioProgramRequest::MusicMVRadioProgramRequest(QObject *parent)
    : MusicAbstractMVRadioRequest(parent)
{

}

void MusicMVRadioProgramRequest::downLoadFinished()
{
    setNetworkAbort(false);

    if(m_reply && m_reply->error() == QNetworkReply::NoError)
    {
        QByteArray bytes = m_reply->readAll();
        bytes = QString(bytes).split("var mvfmdata = ").back().split("$img = ").front().toUtf8();
        bytes.chop(3);

        QJson::Parser parser;
        bool ok;
        const QVariant &data = parser.parse(bytes, &ok);
        if(ok)
        {
            bool contains = false;
            for(const QVariant &var : data.toList())
            {
                if(var.isNull())
                {
                    continue;
                }

                QVariantMap value = var.toMap();
                TTK_NETWORK_QUERY_CHECK();

                MusicResultsItem item;
                item.m_nickName = value["className"].toString();

                for(const QVariant &var : value["fm_list"].toList())
                {
                    if(var.isNull())
                    {
                        continue;
                    }

                    value = var.toMap();
                    TTK_NETWORK_QUERY_CHECK();

                    if(!contains && value["fmId"].toString() == m_searchText)
                    {
                        contains = true;
                        item.m_name = value["fmName"].toString();
                        item.m_id = value["fmId"].toString();
                        item.m_coverUrl = value["imgUrlMv"].toString();

                        Q_EMIT createCategoryItem(item);

                        for(const QVariant &var : value["mvs"].toList())
                        {
                            if(var.isNull())
                            {
                                continue;
                            }

                            value = var.toMap();
                            TTK_NETWORK_QUERY_CHECK();

                            MusicObject::MusicSongInformation musicInfo;
                            musicInfo.m_singerName = MusicUtils::String::illegalCharactersReplaced(value["name"].toString());
                            musicInfo.m_songName = MusicUtils::String::illegalCharactersReplaced(value["name"].toString());
                            if(musicInfo.m_singerName.contains(" - "))
                            {
                                const QStringList &ds = musicInfo.m_singerName.split(" - ");
                                if(ds.count() >= 2)
                                {
                                    musicInfo.m_singerName = ds.front();
                                    musicInfo.m_songName = ds.back();
                                }
                            }
                            musicInfo.m_timeLength = MusicTime::msecTime2LabelJustified(value["time"].toInt());

                            musicInfo.m_songId = value["mvhash"].toString();
                            TTK_NETWORK_QUERY_CHECK();
                            readFromMusicMVAttribute(&musicInfo);
                            TTK_NETWORK_QUERY_CHECK();

                            if(musicInfo.m_songAttrs.isEmpty())
                            {
                                continue;
                            }
                            //
                            MusicSearchedItem item;
                            item.m_songName = musicInfo.m_songName;
                            item.m_singerName = musicInfo.m_singerName;
                            item.m_time = musicInfo.m_timeLength;
                            item.m_albumName.clear();
                            item.m_type.clear();
                            Q_EMIT createSearchedItem(item);
                            m_musicSongInfos << musicInfo;
                        }
                    }
                }
            }
        }
    }

    Q_EMIT downLoadDataChanged(QString());
    deleteAll();
}

void MusicMVRadioProgramRequest::readFromMusicMVAttribute(MusicObject::MusicSongInformation *info)
{
    if(info->m_songId.isEmpty() || !m_manager)
    {
        return;
    }

    const QByteArray &encodedData = MusicUtils::Algorithm::md5(QString("%1kugoumvcloud").arg(info->m_songId).toUtf8()).toHex().toLower();

    QNetworkRequest request;
    request.setUrl(MusicUtils::Algorithm::mdII(KG_MOVIE_INFO_URL, false).arg(QString(encodedData)).arg(info->m_songId));
    request.setRawHeader("User-Agent", MusicUtils::Algorithm::mdII(KG_UA_URL, ALG_UA_KEY, false).toUtf8());
    MusicObject::setSslConfiguration(&request);

    MusicSemaphoreLoop loop;
    QNetworkReply *reply = m_manager->get(request);
    QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop, SLOT(quit()));
    loop.exec();

    if(!reply || reply->error() != QNetworkReply::NoError)
    {
        return;
    }

    QJson::Parser parser;
    bool ok;
    const QVariant &data = parser.parse(reply->readAll(), &ok);
    if(ok)
    {
        QVariantMap value = data.toMap();
        if(!value.isEmpty() && value.contains("mvdata"))
        {
            value = value["mvdata"].toMap();
            QVariantMap mv = value["sd"].toMap();
            if(!mv.isEmpty())
            {
                readFromMusicMVAttribute(info, mv);
            }
            mv = value["hd"].toMap();
            if(!mv.isEmpty())
            {
                readFromMusicMVAttribute(info, mv);
            }
            mv = value["sq"].toMap();
            if(!mv.isEmpty())
            {
                readFromMusicMVAttribute(info, mv);
            }
            mv = value["rq"].toMap();
            if(!mv.isEmpty())
            {
                readFromMusicMVAttribute(info, mv);
            }
        }
    }
}

void MusicMVRadioProgramRequest::readFromMusicMVAttribute(MusicObject::MusicSongInformation *info, const QVariantMap &key)
{
    MusicObject::MusicSongAttribute attr;
    attr.m_url = key["downurl"].toString();
    attr.m_size = MusicUtils::Number::size2Label(key["filesize"].toInt());
    attr.m_format = MusicUtils::String::stringSplitToken(attr.m_url);

    int bitRate = key["bitrate"].toInt() / 1000;
    if(bitRate <= 375)
        attr.m_bitrate = MB_250;
    else if(bitRate > 375 && bitRate <= 625)
        attr.m_bitrate = MB_500;
    else if(bitRate > 625 && bitRate <= 875)
        attr.m_bitrate = MB_750;
    else if(bitRate > 875)
        attr.m_bitrate = MB_1000;

    attr.m_duration = MusicTime::msecTime2LabelJustified(key["timelength"].toInt());
    info->m_songAttrs.append(attr);
}
