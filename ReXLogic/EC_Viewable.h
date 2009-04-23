// For conditions of distribution and use, see copyright notice in license.txt

#ifndef incl_EC_Viewable_h
#define incl_EC_Viewable_h

#include "ComponentInterface.h"
#include "Foundation.h"
#include "RexUUID.h"

namespace RexLogic
{
    /// \todo This component will probably not contain even half of the data that is presented here, but keeping this still here to show
    ///       what kinds of data there will be. Will be refactored to proper positions once the actual component layout is decided. -jj
    class EC_Viewable : public Foundation::ComponentInterface
    {
        DECLARE_EC(EC_Viewable);
    public:
        virtual ~EC_Viewable();

        uint8_t DrawType;
        bool IsVisible;
        bool CastShadows;
        bool LightCreatesShadows;
        bool DescriptionTexture;
        bool ScaleToPrim;
        
        float DrawDistance;
        float LOD;
        
        RexTypes::RexUUID MeshUUID;
        RexTypes::RexUUID ParticleScriptUUID;
        
        //! Animation
        RexTypes::RexUUID AnimationPackageUUID;
        std::string AnimationName;
        float AnimationRate;
        
        //! Materials
        struct MaterialData
        {
            uint8_t Type;
            RexTypes::RexUUID UUID;
        };        
        typedef std::map<uint8_t, MaterialData> MaterialMap;
        MaterialMap Materials;

        void HandleRexPrimData(const uint8_t* primdata);
        void PrintDebug();
    private:
        EC_Viewable(Foundation::ModuleInterface* module);
    };
}

#endif
