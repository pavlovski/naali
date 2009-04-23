#include "StableHeaders.h"
#include "AssetModule.h"
#include "AssetInterface.h"
#include "NetworkEvents.h"
#include "OpenSimProtocolModule.h"
#include "RexProtocolMsgIDs.h"
#include "AssetDefines.h"
#include "AssetEvents.h"
#include "AssetManager.h"
#include "AssetTransfer.h"
#include "RexAsset.h"

using namespace OpenSimProtocol;
using namespace RexTypes;

namespace Asset
{
    const char *AssetManager::DEFAULT_ASSET_CACHE_PATH = "/assetcache";
    const Core::Real AssetManager::DEFAULT_ASSET_TIMEOUT = 60.0;

    AssetManager::AssetManager(Foundation::Framework* framework) : 
        framework_(framework)
    {
        asset_timeout_ = framework_->GetDefaultConfig().DeclareSetting("AssetManager", "Timeout", DEFAULT_ASSET_TIMEOUT);
        Foundation::EventManagerPtr event_manager = framework_->GetEventManager();
        
        event_category_ = event_manager->RegisterEventCategory("Asset");
        event_manager->RegisterEvent(event_category_, Event::ASSET_READY, "AssetReady");
        
        // Create asset cache directory
        cache_path_ = framework_->GetPlatform()->GetApplicationDataDirectory() + DEFAULT_ASSET_CACHE_PATH;
        if (boost::filesystem::exists(cache_path_) == false)
        {
            boost::filesystem::create_directory(cache_path_);
        }
    }
    
    AssetManager::~AssetManager()
    {
    }
    
    Foundation::AssetPtr AssetManager::GetAsset(const std::string& asset_id, Core::asset_type_t asset_type)
    {
        RexUUID asset_uuid(asset_id);
        
        return GetFromCache(asset_uuid);
    }
  
    void AssetManager::RequestAsset(const std::string& asset_id, Core::asset_type_t asset_type)
    {
        RexUUID asset_uuid(asset_id);

        if (GetFromCache(asset_uuid))
            return;

        if (asset_type == RexAT_Texture)
        {
            RequestTexture(asset_uuid);
        }
        else
        {
            RequestOtherAsset(asset_uuid, asset_type);
        }
    }
    
    Foundation::AssetPtr AssetManager::GetIncompleteAsset(const std::string& asset_id, Core::asset_type_t asset_type, Core::uint received)
    {
        if (!received)
            return Foundation::AssetPtr();
            
        RexUUID asset_uuid(asset_id);
        
        AssetTransfer* transfer = GetTransfer(asset_uuid);
        
        // No transfer, either get complete asset or nothing
        if (!transfer)       
            return GetAsset(asset_id, asset_type);
            
        if (transfer->GetReceivedContinuous() >= received)
        {
            // Make new temporary asset for the incomplete data
            RexAsset* new_asset = new RexAsset(transfer->GetAssetId(), transfer->GetAssetType());
            Foundation::AssetPtr asset_ptr(new_asset);
            
            RexAsset::AssetDataVector& data = new_asset->GetDataInternal();
            data.resize(transfer->GetReceivedContinuous());
            transfer->AssembleData(&data[0]);
            
            return asset_ptr;
        }

        // Not enough bytes
        return Foundation::AssetPtr();
    }
    
    bool AssetManager::QueryAssetStatus(const std::string& asset_id, Core::uint& size, Core::uint& received, Core::uint& received_continuous)
    {
        RexUUID asset_uuid(asset_id);
        
        AssetTransfer* transfer = GetTransfer(asset_uuid);
        if (transfer)
        {
            size = transfer->GetSize();
            received = transfer->GetReceived();
            received_continuous = transfer->GetReceivedContinuous();
            return true;
        }
        
        Foundation::AssetPtr asset = GetFromCache(asset_uuid);
        if (asset)
        {
            size = asset->GetSize();
            received = asset->GetSize();
            received_continuous = asset->GetSize();
            return true;
        }
        
        return false;
    }
    
    void AssetManager::Update(Core::f64 frametime)
    {  
        // Get network interface
        boost::shared_ptr<OpenSimProtocol::OpenSimProtocolModule> net = 
            (framework_->GetModuleManager()->GetModule<OpenSimProtocol::OpenSimProtocolModule>(Foundation::Module::MT_OpenSimProtocol)).lock();

        if (!net)
        {
            // Connection lost, do nothing until it returns        
            return;
        }

        AssetTransferMap::iterator i = texture_transfers_.begin();
        while (i != texture_transfers_.end())
        {
            AssetTransfer& transfer = i->second;
            if (!transfer.Ready())
            {
                transfer.AddTime(frametime);
                if (transfer.GetTime() > asset_timeout_)
                {
                    AssetModule::LogInfo("Texture transfer " + transfer.GetAssetId().ToString() + " timed out.");

                    // Send cancel message
                    const OpenSimProtocol::ClientParameters& client = net->GetClientParameters();
                    NetOutMessage *m = net->StartMessageBuilding(RexNetMsgRequestImage);
                    assert(m);
                    
                    m->AddUUID(client.agentID);
                    m->AddUUID(client.sessionID);
    
                    m->SetVariableBlockCount(1);
                    m->AddUUID(transfer.GetAssetId()); // Image UUID
                    m->AddS8(-1); // Discard level, -1 = cancel
                    m->AddF32(0.0); // Download priority, 0 = cancel
                    m->AddU32(0); // Starting packet
                    m->AddU8(RexIT_Normal); // Image type
    
                    net->FinishMessageBuilding(m);

                    // Send transfer canceled event
                    SendAssetCanceled(transfer);

                    texture_transfers_.erase(i);
                }
            }
            ++i;
        }
        
        AssetTransferMap::iterator j = asset_transfers_.begin();
        while (j != asset_transfers_.end())
        {
            AssetTransfer& transfer = j->second;
            if (!transfer.Ready())
            {
                transfer.AddTime(frametime);
                if (transfer.GetTime() > asset_timeout_)
                {
                    AssetModule::LogInfo("Asset transfer " + transfer.GetAssetId().ToString() + " timed out.");

                    // Send cancel message
                    NetOutMessage *m = net->StartMessageBuilding(RexNetMsgTransferAbort);
                    assert(m);
                    m->AddUUID(j->first); // Transfer ID
                    m->AddS32(RexAC_Asset); // Asset channel type
                    net->FinishMessageBuilding(m);
            
                    // Send transfer canceled event
                    SendAssetCanceled(transfer);

                    asset_transfers_.erase(j);
                }
            }
            ++j;
        }
    }
    
    void AssetManager::RequestTexture(const RexUUID& asset_id)
    {
        // Get network interface
        boost::shared_ptr<OpenSimProtocol::OpenSimProtocolModule> net = 
            (framework_->GetModuleManager()->GetModule<OpenSimProtocol::OpenSimProtocolModule>(Foundation::Module::MT_OpenSimProtocol)).lock();
        
        if ((!net) || (!net->IsConnected()))
        {
            // Cannot service request now
            return;
        }
        
        const OpenSimProtocol::ClientParameters& client = net->GetClientParameters();
        
        if (texture_transfers_.find(asset_id) != texture_transfers_.end())
            return;
        
        AssetTransfer new_transfer;
        new_transfer.SetAssetId(asset_id);
        new_transfer.SetAssetType(RexAT_Texture);
        texture_transfers_[asset_id] = new_transfer;
    
        AssetModule::LogInfo("Requesting texture " + asset_id.ToString());

        NetOutMessage *m = net->StartMessageBuilding(RexNetMsgRequestImage);
        assert(m);
        
        m->AddUUID(client.agentID);
        m->AddUUID(client.sessionID);
        
        m->SetVariableBlockCount(1);
        m->AddUUID(asset_id); // Image UUID
        m->AddS8(0); // Discard level
        m->AddF32(100.0); // Download priority
        m->AddU32(0); // Starting packet
        m->AddU8(RexIT_Normal); // Image type
        
        net->FinishMessageBuilding(m);
    }
    
    void AssetManager::RequestOtherAsset(const RexUUID& asset_id, Core::uint asset_type)
    {
        // Get network interface
        boost::shared_ptr<OpenSimProtocol::OpenSimProtocolModule> net = 
            (framework_->GetModuleManager()->GetModule<OpenSimProtocol::OpenSimProtocolModule>(Foundation::Module::MT_OpenSimProtocol)).lock();

        if ((!net) || (!net->IsConnected()))
        {
            // Cannot service request now
            return;
        }
            
        // Asset transfers are keyed by transfer id, not asset id, so have to search in a bit cumbersome way
        AssetTransferMap::const_iterator i = asset_transfers_.begin();
        while (i != asset_transfers_.end())
        {
            if (i->second.GetAssetId() == asset_id)
                return;

            ++i;
        }

        RexUUID transfer_id;
        transfer_id.Random();
        
        AssetTransfer new_transfer;
        new_transfer.SetAssetId(asset_id);
        new_transfer.SetAssetType(asset_type);
        asset_transfers_[transfer_id] = new_transfer;
        
        AssetModule::LogInfo("Requesting asset " + asset_id.ToString());
        
        NetOutMessage *m = net->StartMessageBuilding(RexNetMsgTransferRequest);
        assert(m);
        
        m->AddUUID(transfer_id); // Transfer ID
        m->AddS32(RexAC_Asset); // Asset channel type
        m->AddS32(RexAS_Asset); // Asset source type
        m->AddF32(100.0); // Download priority
        
        Core::u8 asset_info[20]; // Asset info block with UUID and type
        memcpy(&asset_info[0], &asset_id.data, 16);
        memcpy(&asset_info[16], &asset_type, 4);
        m->AddBuffer(20, asset_info);
        
        net->FinishMessageBuilding(m);
    }
    
    void AssetManager::HandleTextureHeader(NetInMessage* msg)
    {
        RexUUID asset_id = msg->ReadUUID();
        AssetTransferMap::iterator i = texture_transfers_.find(asset_id);
        if (i == texture_transfers_.end())
        {
            AssetModule::LogWarning("Data received for nonexisting texture transfer " + asset_id.ToString());
            return;
        }
        
        AssetTransfer& transfer = i->second;
        
        Core::u8 codec = msg->ReadU8();
        Core::u32 size = msg->ReadU32();
        Core::u16 packets = msg->ReadU16();
        
        transfer.SetSize(size);
        
        Core::uint data_size; 
        const Core::u8* data = msg->ReadBuffer(&data_size); // ImageData block
        transfer.ReceiveData(0, data, data_size);
                
        SendAssetProgress(transfer);

        if (transfer.Ready())
        {
            StoreAsset(transfer);
            texture_transfers_.erase(i);
        }
    }
    
    void AssetManager::HandleTextureData(NetInMessage* msg)
    {
        RexUUID asset_id = msg->ReadUUID();
        AssetTransferMap::iterator i = texture_transfers_.find(asset_id);
        if (i == texture_transfers_.end())
        {
            AssetModule::LogWarning("Data received for nonexisting texture transfer " + asset_id.ToString());
            return;
        }
        
        AssetTransfer& transfer = i->second;
        
        Core::u16 packet_index = msg->ReadU16();
        
        Core::uint data_size; 
        const Core::u8* data = msg->ReadBuffer(&data_size); // ImageData block
        transfer.ReceiveData(packet_index, data, data_size);
      
        SendAssetProgress(transfer);

        if (transfer.Ready())
        {
            StoreAsset(transfer);
            texture_transfers_.erase(i);
        }
    }
    
    void AssetManager::HandleTextureCancel(NetInMessage* msg)
    {
        RexUUID asset_id = msg->ReadUUID();
        AssetTransferMap::iterator i = texture_transfers_.find(asset_id);
        if (i == texture_transfers_.end())
        {
            AssetModule::LogWarning("Cancel received for nonexisting texture transfer " + asset_id.ToString());
            return;
        }

        AssetTransfer& transfer = i->second;

        // Send transfer canceled event
        SendAssetCanceled(transfer);

        AssetModule::LogInfo("Transfer of texture " + asset_id.ToString() + " canceled");
        texture_transfers_.erase(i);
    }
    
    void AssetManager::HandleAssetHeader(NetInMessage* msg)
    {
        RexUUID transfer_id = msg->ReadUUID();
        AssetTransferMap::iterator i = asset_transfers_.find(transfer_id);
        if (i == asset_transfers_.end())
        {
            AssetModule::LogWarning("Data received for nonexisting asset transfer " + transfer_id.ToString());
            return;
        }
        
        AssetTransfer& transfer = i->second;
        
        Core::s32 channel_type = msg->ReadS32();
        Core::s32 target_type = msg->ReadS32();
        Core::s32 status = msg->ReadS32();
        Core::s32 size = msg->ReadS32();
        
        if ((status != RexTS_Ok) && (status != RexTS_Done))
        {
            AssetModule::LogInfo("Transfer for asset " + transfer.GetAssetId().ToString() + " canceled with code " + Core::ToString<Core::s32>(status));
            asset_transfers_.erase(i);
            return;
        }
        
        transfer.SetSize(size);
        
        SendAssetProgress(transfer);

        // We may get data packets before header, so check if all already received
        if (transfer.Ready())
        {
            StoreAsset(transfer);
            asset_transfers_.erase(i);
        }
    }
    
    void AssetManager::HandleAssetData(NetInMessage* msg)
    {
        RexUUID transfer_id = msg->ReadUUID();
        AssetTransferMap::iterator i = asset_transfers_.find(transfer_id);
        if (i == asset_transfers_.end())
        {
            AssetModule::LogWarning("Data received for nonexisting asset transfer " + transfer_id.ToString());
            return;
        }
        
        AssetTransfer& transfer = i->second;
        
        Core::s32 channel_type = msg->ReadS32();
        Core::s32 packet_index = msg->ReadS32();
        Core::s32 status = msg->ReadS32();
        
        if ((status != RexTS_Ok) && (status != RexTS_Done))
        {
            AssetModule::LogInfo("Transfer for asset " + transfer.GetAssetId().ToString() + " canceled with code " + Core::ToString<Core::s32>(status));

            // Send transfer canceled event
            SendAssetCanceled(transfer);
            
            asset_transfers_.erase(i);
            return;
        }
        
        Core::uint data_size; 
        const Core::u8* data = msg->ReadBuffer(&data_size); // Data block
        transfer.ReceiveData(packet_index, data, data_size);
        
        SendAssetProgress(transfer);

        if (transfer.Ready())
        {
            StoreAsset(transfer);
            asset_transfers_.erase(i);
        }
    }
    
    void AssetManager::HandleAssetCancel(NetInMessage* msg)
    {
        RexUUID transfer_id = msg->ReadUUID();
        AssetTransferMap::iterator i = asset_transfers_.find(transfer_id);
        if (i == asset_transfers_.end())
        {
            AssetModule::LogWarning("Cancel received for nonexisting asset transfer " + transfer_id.ToString());
            return;
        }
        
        AssetTransfer& transfer = i->second;

        // Send transfer canceled event
        SendAssetCanceled(transfer);

        AssetModule::LogInfo("Transfer for asset " + transfer.GetAssetId().ToString() + " canceled");
        asset_transfers_.erase(i);
    }

    void AssetManager::SendAssetProgress(AssetTransfer& transfer)
    {
        Foundation::EventManagerPtr event_manager = framework_->GetEventManager();
        Event::AssetProgress event_data(transfer.GetAssetId().ToString(), transfer.GetAssetType(), transfer.GetSize(), transfer.GetReceived(), transfer.GetReceivedContinuous());
        event_manager->SendEvent(event_category_, Event::ASSET_PROGRESS, &event_data);
    }

    void AssetManager::SendAssetCanceled(AssetTransfer& transfer)
    {
        Foundation::EventManagerPtr event_manager = framework_->GetEventManager();
        Event::AssetCanceled event_data(transfer.GetAssetId().ToString(), transfer.GetAssetType());
        event_manager->SendEvent(event_category_, Event::ASSET_CANCELED, &event_data);
    }

    void AssetManager::StoreAsset(AssetTransfer& transfer)
    {
        const RexUUID& asset_id = transfer.GetAssetId();
        
        AssetModule::LogInfo("Storing complete asset " + asset_id.ToString());

        // Store to memory cache
        Foundation::AssetPtr new_asset = Foundation::AssetPtr(new RexAsset(transfer.GetAssetId(), transfer.GetAssetType()));
        assets_[asset_id] = new_asset;
        
        RexAsset::AssetDataVector& data = checked_static_cast<RexAsset*>(new_asset.get())->GetDataInternal();
        data.resize(transfer.GetReceived());
        transfer.AssembleData(&data[0]);
        
        // Store to disk cache
        boost::filesystem::path file_path(cache_path_ + "/" + asset_id.ToString());
        std::ofstream filestr(file_path.native_directory_string().c_str(), std::ios::out | std::ios::binary);
        if (filestr.good())
        {
            Core::uint type = transfer.GetAssetType();
            
            // Store first the asset type, then the actual data
            filestr.write((const char *)&type, sizeof(type));
            filestr.write((const char *)&data[0],data.size());
            filestr.close();
        }
        else
        {
            AssetModule::LogError("Error storing asset " + asset_id.ToString() + " to cache.");
        }
        
        // Send asset ready event
        Foundation::EventManagerPtr event_manager = framework_->GetEventManager();
        Event::AssetReady event_data(new_asset->GetId(), new_asset->GetType(), new_asset);
        event_manager->SendEvent(event_category_, Event::ASSET_READY, &event_data);
    }
    
    Foundation::AssetPtr AssetManager::GetFromCache(const RexTypes::RexUUID& asset_id)
    {
        AssetMap::iterator i = assets_.find(asset_id);
        if (i != assets_.end())
            return i->second;

        // If transfer in progress, do not check disk cache again
        if (GetTransfer(asset_id))
            return Foundation::AssetPtr();
        boost::filesystem::path file_path(cache_path_ + "/" + asset_id.ToString());
        
        std::ifstream filestr(file_path.native_directory_string().c_str(), std::ios::in | std::ios::binary);
        if (filestr.good())
        {
            filestr.seekg(0, std::ios::end);
            Core::uint length = filestr.tellg();
            filestr.seekg(0, std::ios::beg);

            Core::uint type;
            if (length > sizeof(type))
            {
                length -= sizeof(type);
                
                filestr.read((char *)&type, sizeof(type));
        
                RexAsset* new_asset = new RexAsset(asset_id, type);
                assets_[asset_id] = Foundation::AssetPtr(new_asset);
                
                RexAsset::AssetDataVector& data = new_asset->GetDataInternal();
                data.resize(length);
                filestr.read((char *)&data[0], length);
                filestr.close();
                
                return assets_[asset_id];
            }
            else
            {
                AssetModule::LogError("Malformed asset file " + asset_id.ToString() + " found in cache.");
            }
            
            filestr.close();
        }
        
        return Foundation::AssetPtr();
    }
    
    AssetTransfer* AssetManager::GetTransfer(const RexTypes::RexUUID& asset_id)
    {
        AssetTransferMap::iterator i = texture_transfers_.find(asset_id);
        if (i != texture_transfers_.end())
            return &i->second;

        AssetTransferMap::iterator j = asset_transfers_.begin();
        while (j != asset_transfers_.end())
        {
            if (j->second.GetAssetId() == asset_id)
                return &j->second;
            ++j;
        }
        
        return NULL;
    }
}