// Exercise the production identity callbacks against a live two-controller
// fixture. Policy corruption is restored before either controller is ticked.
static void test_projection_scope_is_read_identity_not_edit_permission(){
    Party party;
    for(unsigned author=0;author<2;++author){
        side=int(author);
        auto& controller=author?runtime_guest::controller:runtime_host::controller;
        auto& view=const_cast<es::View&>(controller->view());
        auto& document=const_cast<editor::Document&>(controller->state().document);
        const auto original_view=view;const auto original_document=document;
        const editor_native::GestureScope expected{view.context.project,view.context.epoch,view.local};
        auto projection=[&]{return native[author].callbacks.projection_scope();};
        auto input=[&]{return native[author].callbacks.gesture_scope();};
        const auto snapshot=native[author].snapshot;const auto restores=native[author].restores,applies=native[author].apply_calls;
        const auto writes=controller->state().history;
        CHECK(projection()==expected&&input()==expected);
        auto& awaiting=author?runtime_guest::awaiting_operation:runtime_host::awaiting_operation;
        CHECK(!awaiting);awaiting=editor::OperationId{view.local,999,view.context.epoch};
        for(const auto phase:{es::Phase::Ready,es::Phase::Preparing,es::Phase::Playing,es::Phase::Returning}){
            view.phase=phase;
            CHECK(projection()==expected);
            CHECK(input()==editor_native::GestureScope{});
        }
        awaiting.reset();view.phase=es::Phase::Ready;CHECK(input()==expected);
        for(const auto phase:{es::Phase::Idle,es::Phase::Offered,es::Phase::Joining,es::Phase::Syncing,es::Phase::Frozen,es::Phase::Disconnected}){
            view.phase=phase;CHECK(projection()==editor_native::GestureScope{});
        }
        view.phase=es::Phase::Ready;
        auto denied=[&]{CHECK(projection()==editor_native::GestureScope{});};
        auto& attached=author?runtime_guest::native_attached:runtime_host::native_attached;
        auto& frozen=author?runtime_guest::frozen:runtime_host::frozen;
        auto& closing=author?runtime_guest::closing:runtime_host::closing;
        auto& exiting=author?runtime_guest::native_exit_requested:runtime_host::native_exit_requested;
        auto& saving=author?runtime_guest::save_publication_pending:runtime_host::save_publication_pending;
        for(auto* flag:{&frozen,&closing,&exiting,&saving}){CHECK(!*flag);*flag=true;denied();*flag=false;}
        CHECK(attached);attached=false;denied();attached=true;
        auto& export_request=author?runtime_guest::pending_export_request:runtime_host::pending_export_request;
        CHECK(!export_request);export_request=1;denied();export_request=0;
        auto exporting=[&](bool value){
            if(author){if(value)runtime_guest::exporting.emplace();else runtime_guest::exporting.reset();}
            else{if(value)runtime_host::exporting.emplace();else runtime_host::exporting.reset();}
        };
        auto recovering=[&](bool value){
            if(author){if(value)runtime_guest::recovery.emplace();else runtime_guest::recovery.reset();}
            else{if(value)runtime_host::recovery.emplace();else runtime_host::recovery.reset();}
        };
        auto teardown=[&](bool value){
            if(author){if(value)runtime_guest::transport_teardown.emplace();else runtime_guest::transport_teardown.reset();}
            else{if(value)runtime_host::transport_teardown.emplace();else runtime_host::transport_teardown.reset();}
        };
        exporting(true);denied();exporting(false);recovering(true);denied();recovering(false);teardown(true);denied();teardown(false);
        auto& context=author?runtime_guest::C:runtime_host::C;
        auto* connection=context.session;context.session=nullptr;denied();context.session=connection;
        net::Session disconnected;context.session=&disconnected;denied();context.session=connection;
        auto& transport=author?runtime_guest::observed_transport:runtime_host::observed_transport;
        const auto original_transport=transport;++transport;denied();transport=original_transport;
        view.context.project={};denied();view.context=original_view.context;
        view.context.epoch=0;denied();view.context=original_view.context;
        view.local.generation=0;denied();view.local=original_view.local;
        view.local.slot=uint8_t(1-author);denied();view.local=original_view.local;
        ++document.project[0];denied();document.project=original_document.project;
        ++document.epoch;denied();document.epoch=original_document.epoch;
        CHECK(projection()==expected&&input()==expected);
        CHECK(controller->state().document==original_document&&controller->state().history==writes);
        CHECK(native[author].snapshot.bytes==snapshot.bytes&&native[author].snapshot.document==snapshot.document);
        CHECK(native[author].restores==restores&&native[author].apply_calls==applies);
        view=original_view;
    }
}
