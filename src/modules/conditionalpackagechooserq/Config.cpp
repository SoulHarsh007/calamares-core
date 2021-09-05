/* === This file is part of Calamares - <https://calamares.io> ===
 *
 *   SPDX-FileCopyrightText: 2021 Adriaan de Groot <groot@kde.org>
 *   SPDX-FileCopyrightText: 2021 Anke Boersma <demm@kaosx.us>
 *   SPDX-FileCopyrightText: 2021 shivanandvp <shivanandvp@rebornos.org>
 *   SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   Calamares is Free Software: see the License-Identifier above.
 *
 */

#include "Config.h"

#ifdef HAVE_APPDATA
#include "ItemAppData.h"
#endif

#ifdef HAVE_APPSTREAM
#include "ItemAppStream.h"
#include <AppStreamQt/pool.h>
#include <memory>
#endif


#include "GlobalStorage.h"
#include "JobQueue.h"
#include "packages/Globals.h"
#include "utils/Logger.h"
#include "utils/Variant.h"
#include "viewpages/QmlViewStep.h"

const NamedEnumTable< PackageChooserMode >&
packageChooserModeNames()
{
    static const NamedEnumTable< PackageChooserMode > names {
        { "optional", PackageChooserMode::Optional },
        { "required", PackageChooserMode::Required },
        { "optionalmultiple", PackageChooserMode::OptionalMultiple },
        { "requiredmultiple", PackageChooserMode::RequiredMultiple },
        // and a bunch of aliases
        { "zero-or-one", PackageChooserMode::Optional },
        { "radio", PackageChooserMode::Required },
        { "one", PackageChooserMode::Required },
        { "set", PackageChooserMode::OptionalMultiple },
        { "zero-or-more", PackageChooserMode::OptionalMultiple },
        { "multiple", PackageChooserMode::RequiredMultiple },
        { "one-or-more", PackageChooserMode::RequiredMultiple }
    };
    return names;
}

const NamedEnumTable< PackageChooserMethod >&
PackageChooserMethodNames()
{
    static const NamedEnumTable< PackageChooserMethod > names {
        { "legacy", PackageChooserMethod::Legacy },
        { "custom", PackageChooserMethod::Legacy },
        { "contextualprocess", PackageChooserMethod::Legacy },
        { "packages", PackageChooserMethod::Packages },
    };
    return names;
}

Config::Config( QObject* parent )
    : Calamares::ModuleSystem::Config( parent )
    , m_model( new PackageListModel( this ) )
    , m_mode( PackageChooserMode::Required )
    , m_selections( QStringList() )
    , m_packagesFromOnlyThisInstance( QStringList() )
    , m_displayedEntryIds( QStringList() )
    , m_displayedEntryNames( QStringList() )
    , m_displayedEntryDescriptions( QStringList() )
    , m_displayedEntryScreenshots( QVector<QString>() )
    , m_displayedEntryPackages( QVector<QStringList>() )
    , m_displayedEntrySelectedStates( QVector<bool>() )
{
}

Config::~Config() {}

const PackageItem&
Config::introductionPackage() const
{
    for ( int i = 0; i < m_model->packageCount(); ++i )
    {
        const auto& package = m_model->packageData( i );
        if ( package.isNonePackage() )
        {
            return package;
        }
    }

    static PackageItem* defaultIntroduction = nullptr;
    if ( !defaultIntroduction )
    {
        const auto name = QT_TR_NOOP( "Package Selection" );
        const auto description
            = QT_TR_NOOP( "Please pick a product from the list. The selected product will be installed." );
        defaultIntroduction = new PackageItem( QString(), name, description, false );
        defaultIntroduction->screenshot = QStringLiteral( ":/images/no-selection.png" );
        defaultIntroduction->name = CalamaresUtils::Locale::TranslatedString( name, metaObject()->className() );
        defaultIntroduction->description
            = CalamaresUtils::Locale::TranslatedString( description, metaObject()->className() );
    }
    return *defaultIntroduction;
}

void
Config::pageLeavingTasks() const
{

    Calamares::JobQueue::instance()->globalStorage()->insert( m_outputConditionKey, m_selections );

    if ( m_method == PackageChooserMethod::Legacy )
    {
        //QString value = m_selections.join( ',' );
        QString value = ( m_pkgc );
        Calamares::JobQueue::instance()->globalStorage()->insert( m_id, value );
        cDebug() << m_id<< "selected" << value;
    }
    else if ( m_method == PackageChooserMethod::Packages )
    {
        cDebug() << "m_selections: " << m_selections;
        QStringList packageNames = m_model->getInstallPackagesForNames( m_selections );
        cDebug() << m_defaultId << "packages to install" << packageNames;
        CalamaresUtils::Packages::setGSPackageAdditions(
            Calamares::JobQueue::instance()->globalStorage(), m_defaultId, packageNames );
    }
    else
    {
        cWarning() << "Unknown packagechooser method" << smash( m_method );
    }
}

void
Config::setPkgc( const QString& pkgc )
{
    m_pkgc = pkgc;
    emit pkgcChanged( m_pkgc );
}

QString
Config::prettyStatus() const
{
    return tr( "Install option: <strong>%1</strong>" ).arg( m_pkgc );
}

static void
fillModel( PackageListModel* model, const QVariantList& items )
{
    if ( items.isEmpty() )
    {
        cWarning() << "No *items* for ConditionalPackageChooser module.";
        return;
    }

#ifdef HAVE_APPSTREAM
    std::unique_ptr< AppStream::Pool > pool;
    bool poolOk = false;
#endif

    cDebug() << "Loading ConditionalPackageChooser model items from config";
    int item_index = 0;
    for ( const auto& item_it : items )
    {
        ++item_index;
        QVariantMap item_map = item_it.toMap();
        if ( item_map.isEmpty() )
        {
            cWarning() << "ConditionalPackageChooser entry" << item_index << "is not valid.";
            continue;
        }

        if ( item_map.contains( "appdata" ) )
        {
#ifdef HAVE_XML
            model->addPackage( fromAppData( item_map ) );
#else
            cWarning() << "Loading AppData XML is not supported.";
#endif
        }
        else if ( item_map.contains( "appstream" ) )
        {
#ifdef HAVE_APPSTREAM
            if ( !pool )
            {
                pool = std::make_unique< AppStream::Pool >();
                pool->setLocale( QStringLiteral( "ALL" ) );
                poolOk = pool->load();
            }
            if ( pool && poolOk )
            {
                model->addPackage( fromAppStream( *pool, item_map ) );
            }
#else
            cWarning() << "Loading AppStream data is not supported.";
#endif
        }
        else
        {
            model->addPackage( PackageItem( item_map ) );
        }
    }
    cDebug() << Logger::SubEntry << "Loaded PackageChooser with" << model->packageCount() << "entries.";
}

void
Config::setConfigurationMap( const QVariantMap& configurationMap )
{
    m_mode = packageChooserModeNames().find( CalamaresUtils::getString( configurationMap, "mode" ),
                                             PackageChooserMode::Required );
    m_method = PackageChooserMethodNames().find( CalamaresUtils::getString( configurationMap, "method" ),
                                                 PackageChooserMethod::Packages );
    m_pkgc = CalamaresUtils::getString( configurationMap, "pkgc" );
    m_outputConditionKey = CalamaresUtils::getString( configurationMap, "outputconditionkey" );
    m_promptMessage = CalamaresUtils::getString( configurationMap, "promptmessage" ); 

    if ( m_method == PackageChooserMethod::Legacy )
    {
        const QString configId = CalamaresUtils::getString( configurationMap, "id" );
        const QString base = QStringLiteral( "packagechooser_" );
        if ( configId.isEmpty() )
        {
            if ( m_defaultId.id().isEmpty() )
            {
                // We got nothing to work with
                m_id = base;
            }
            else
            {
                m_id = base + m_defaultId.id();
            }
            cDebug() << "Using default ID" << m_id << "from" << m_defaultId.toString();
        }
        else
        {
            m_id = base + configId;
            cDebug() << "Using configured ID" << m_id;
        }
    }

    if ( configurationMap.contains( "items" ) )
    {
        fillModel( m_model, configurationMap.value( "items" ).toList() );
    }

    QString default_item_id = CalamaresUtils::getString( configurationMap, "default" );
    if ( !default_item_id.isEmpty() )
    {
        for ( int item_n = 0; item_n < m_model->packageCount(); ++item_n )
        {
            QModelIndex item_idx = m_model->index( item_n, 0 );
            QVariant item_id = m_model->data( item_idx, PackageListModel::IdRole );

            if ( item_id.toString() == default_item_id )
            {
                m_defaultModelIndex = item_idx;
                break;
            }
        }
    }

    cDebug() << "outputConditionKey: " << m_outputConditionKey;
    cDebug() << "promptMessage: " << m_promptMessage;

    m_configurationMapSet = true;

    updateDisplayedData();
}

void Config::addSelection(const QString& selection)
{

    if ( m_selections.contains(selection) ) {
        cWarning() << m_defaultId << " Selection " << selection << " already exists in the list of selections. This is a bug";
        return;
    }

    if ( (m_mode == PackageChooserMode::Optional || m_mode == PackageChooserMode::Required) && m_selections.length() >= 1){
        cDebug() << m_defaultId << "Clearing other selections since at most one entry can be selected...";
        m_displayedEntrySelectedStates.fill(false);
        m_displayedEntrySelectedStates.replace(m_displayedEntryIds.indexOf(selection), true);

        if (m_mode == PackageChooserMode::Optional) {
            m_selections.clear();
        }

        emit displayedEntrySelectedStatesChanged();
    }

    cDebug() << m_defaultId << " Adding " << selection << " as a selection...";
    m_selections.append(selection);
    cDebug() << "m_selections: " << m_selections;
    refreshNextButtonStatus();
}

void Config::removeSelection(const QString& selection)
{

    if ( !m_selections.contains(selection) ) {
        cWarning() << m_defaultId << " Selection " << selection << " did not exist in the list of selections while deselecting. This is a bug";
        return;
    }

    // if ( (m_mode == PackageChooserMode::Required || m_mode == PackageChooserMode::RequiredMultiple) && m_selections.length() <= 1){
    //     cDebug() << m_defaultId << " Not Removing " << selection << " since at least one selection is required...";
    //     m_displayedEntrySelectedStates.replace(m_displayedEntryIds.indexOf(selection), true);
    //     return;
    // }

    cDebug() << m_defaultId << " Removing " << selection << " from selections...";
    m_selections.removeAll(selection);
    cDebug() << "m_selections: " << m_selections;
    refreshNextButtonStatus();
}

bool Config::refreshNextButtonStatus() {
    if ( (m_mode == PackageChooserMode::Required || m_mode == PackageChooserMode::RequiredMultiple) && m_selections.length() < 1 )
    {
        emit nextStatusChanged( false );
        return false;
    }
    else {
        emit nextStatusChanged( true );
        return true;
    }
    cError() << "This message should not be reachable.";
    return false;
}

void Config::updateDisplayedData()
{
    if ( !m_configurationMapSet )
    {
        return;
    }

    m_displayedEntryIds.clear();
    m_displayedEntryNames.clear();
    m_displayedEntryDescriptions.clear();
    m_displayedEntryScreenshots.clear();
    m_displayedEntryPackages.clear();
    m_displayedEntrySelectedStates.clear();

    PackageItem displayedEntryData;
    bool include_displayedEntry;
    Calamares::GlobalStorage* globalStorage = Calamares::JobQueue::instance()->globalStorage();
    QString key;
    QString value;
    bool entries_changed = false;
    for(int i=0; i< m_model-> packageCount(); i++) {
        displayedEntryData = m_model -> packageData(i);
        include_displayedEntry = true;
        for(int j=0; j<displayedEntryData.whenKeyValuePairs.length()-1; j += 2) 
        {
            key = displayedEntryData.whenKeyValuePairs[j];
            value = displayedEntryData.whenKeyValuePairs[j+1];
            if( globalStorage->contains(key) ) {
                if( !value.startsWith('-') && !globalStorage->value(key).toStringList().contains(value, Qt::CaseInsensitive) )
                {
                    include_displayedEntry = false;
                    cDebug() << "Skipping entry \"" << displayedEntryData.id << "\" because the value \"" << value << "\" does not exist in the key \"" << key <<"\".";
                    break;
                }
                else if ( value.startsWith('-') && globalStorage->value(key).toStringList().contains(value, Qt::CaseInsensitive) )
                {
                    include_displayedEntry = false;
                    cDebug() << "Skipping entry \"" << displayedEntryData.id << "\" because the value \"" << value << "\" exists in the key \"" << key <<"\".";
                    break;
                }
                else
                {
                    continue;
                }
            }
            else 
            {
                include_displayedEntry = false;
                cDebug() << "Skipping entry \"" << displayedEntryData.id << "\" because the key \"" << key << "\" does not exist.";
                break;
            }
        }
        if ( include_displayedEntry ) 
        {
            entries_changed = true;
            m_displayedEntryIds.append(displayedEntryData.id);  
            m_displayedEntryNames.append(displayedEntryData.name.get());    
            m_displayedEntryDescriptions.append(displayedEntryData.description.get());
            m_displayedEntryScreenshots.append(displayedEntryData.screenshot);
            m_displayedEntryPackages.append(displayedEntryData.packageNames);
            m_displayedEntrySelectedStates.append(displayedEntryData.selected);
        }
    }
    if ( entries_changed ) 
    {
        // emit displayedEntryIdsChanged(m_displayedEntryIds);
        // emit displayedEntryNamesChanged(m_displayedEntryNames);
        // emit displayedEntryDescriptionsChanged(m_displayedEntryDescriptions);
        // emit displayedEntryScreenshotsChanged(m_displayedEntryScreenshots);    
        // emit displayedEntryPackagesChanged(m_displayedEntryPackages);
        // emit displayedEntrySelectedStatesChanged(m_displayedEntrySelectedStates);

        emit displayedEntryIdsChanged();
        emit displayedEntryNamesChanged();
        emit displayedEntryDescriptionsChanged();
        emit displayedEntryScreenshotsChanged();    
        emit displayedEntryPackagesChanged();
        emit displayedEntrySelectedStatesChanged();
    }

    cDebug() << "displayedEntryIds: " << m_displayedEntryIds;
    cDebug() << "displayedEntryNames: " << m_displayedEntryNames;
    cDebug() << "displayedEntryDescriptions: " << m_displayedEntryDescriptions;
    cDebug() << "displayedEntryScreenshots: " << m_displayedEntryScreenshots;
    cDebug() << "displayedEntryPackages: " << m_displayedEntryPackages;
    cDebug() << "displayedEntrySelectedStates: " << m_displayedEntrySelectedStates;
}

void Config::resetSelections() const
{
    m_selections.clear();
}