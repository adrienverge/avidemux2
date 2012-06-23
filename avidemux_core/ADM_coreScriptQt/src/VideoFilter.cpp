#include "VideoFilter.h"
#include "VideoOutput.h"
#include "ADM_coreVideoFilterFunc.h"
#include "ADM_videoFilterBridge.h"
#include "MyQScriptEngine.h"

extern BVector <ADM_VideoFilterElement> ADM_VideoFilters;

namespace ADM_qtScript
{
    class ADM_videoFilterKludgeCollaborator : public ADM_coreVideoFilter
    {
    private:
        FilterInfo _dummyInfo;

    public:
        ADM_videoFilterKludgeCollaborator() : ADM_coreVideoFilter(NULL, NULL) {}

        bool getNextFrame(uint32_t *frameNumber, ADMImage *image)
        {
            return false;
        }

        FilterInfo *getInfo(void)
        {
            return &this->_dummyInfo;
        }

        bool getCoupledConf(CONFcouple **couples)
        {
            *couples = NULL;

            return true;
        }

        void setCoupledConf(CONFcouple *couples) {}
    };

    VideoFilter::VideoFilter(
        QScriptEngine *engine, IEditor *editor, ADM_vf_plugin *plugin) : QtScriptConfigObject(editor)
    {
        this->filterPlugin = plugin;
        this->_filter = filterPlugin->create(new ADM_videoFilterKludgeCollaborator(), NULL);
        this->_filter->getCoupledConf(&this->_defaultConf);
        this->_attachedToFilterChain = false;

        this->_configObject = this->createConfigContainer(
                                  engine, QtScriptConfigObject::defaultConfigGetterSetter);
    }

    VideoFilter::VideoFilter(
        QScriptEngine *engine, IEditor *editor, ADM_VideoFilterElement *element) : QtScriptConfigObject(editor)
    {
        this->filterPlugin = ADM_vf_getPluginFromTag(element->tag);
        ADM_coreVideoFilter *filter = filterPlugin->create(new ADM_videoFilterKludgeCollaborator(), NULL);

        filter->getCoupledConf(&this->_defaultConf);
        delete filter;

        this->_filter = element->instance;
        this->_attachedToFilterChain = true;
        this->_configObject = this->createConfigContainer(
                                  engine, QtScriptConfigObject::defaultConfigGetterSetter);
    }

    VideoFilter::~VideoFilter()
    {
        delete this->_defaultConf;

        if (!this->_attachedToFilterChain)
        {
            delete this->_filter;
        }
    }

    QScriptValue VideoFilter::constructor(QScriptContext *context, QScriptEngine *engine)
    {
        if (context->isCalledAsConstructor())
        {
            VideoFilter *videoFilterProto = qobject_cast<VideoFilter*>(
                                                context->thisObject().prototype().toQObject());
            VideoFilter *videoFilter = new VideoFilter(
                engine, static_cast<MyQScriptEngine*>(engine)->wrapperEngine->editor(),
                videoFilterProto->filterPlugin);

            return engine->newQObject(videoFilter, QScriptEngine::ScriptOwnership);
        }

        return engine->undefinedValue();
    }

    QScriptValue VideoFilter::getConfiguration()
    {
        return this->_configObject;
    }

    void VideoFilter::resetConfiguration(void)
    {
        if (this->verifyFilter())
        {
            this->_filter->setCoupledConf(this->_defaultConf);
        }
    }

    QString VideoFilter::getName()
    {
        return this->filterPlugin->getDisplayName();
    }

    QScriptValue VideoFilter::getVideoOutput()
    {
        if (this->isFilterUsed())
        {
            return this->engine()->newQObject(new VideoOutput(this->_editor, this->_filter->getInfo()));
        }
        else
        {
            return this->engine()->undefinedValue();
        }
    }

    void VideoFilter::getConfCouple(CONFcouple** conf, const QString& containerName)
    {
        if (this->verifyFilter())
        {
            this->_filter->getCoupledConf(conf);
        }
        else
        {
            *conf = NULL;
        }
    }

    void VideoFilter::setConfCouple(CONFcouple* conf, const QString& containerName)
    {
        if (this->verifyFilter())
        {
            this->_filter->setCoupledConf(conf);
        }
    }

    bool VideoFilter::isFilterUsed()
    {
        return this->_attachedToFilterChain;
    }

    void VideoFilter::setFilterAsUsed(int trackObjectId)
    {
        this->_trackObjectId = trackObjectId;
        this->_attachedToFilterChain = true;
    }

    bool VideoFilter::verifyFilter()
    {
        if (!this->_attachedToFilterChain)
        {
            return true;
        }

        bool found = false;

        for (int filterIndex = 0; filterIndex < ADM_VideoFilters.size(); filterIndex++)
        {
            ADM_VideoFilterElement *element = &ADM_VideoFilters[filterIndex];

            if (this->_filter == element->instance && this->_trackObjectId == element->objectId)
            {
                found = true;
                break;
            }
        }

        return found;
    }
}